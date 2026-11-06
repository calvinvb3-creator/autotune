#!/usr/bin/env python3
"""Build a real Installer.app product archive from tested native plug-ins.

No placeholder payloads or cross-platform .pkg emulation. Apple tools run only
on macOS. Release mode requires Developer ID Application/Installer identities
and a notarytool Keychain profile; local mode is explicitly unsigned.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
import zipfile

ROOT = Path(__file__).resolve().parents[1]
FORMATS = {
    'au': ('AU', 'Pitch Workbench.component', 'Library/Audio/Plug-Ins/Components', 'Audio Unit — Logic Pro'),
    'vst3': ('VST3', 'Pitch Workbench.vst3', 'Library/Audio/Plug-Ins/VST3', 'VST3 — compatible audio hosts'),
}

def run(args: list[str | Path], *, capture: bool = False) -> str:
    result = subprocess.run([str(a) for a in args], check=True, text=True,
                            stdout=subprocess.PIPE if capture else None)
    return result.stdout or ''

def metadata(root: Path) -> dict:
    value = json.loads((root / 'version.json').read_text())
    if not re.fullmatch(r'\d+\.\d+\.\d+', value['version']):
        raise ValueError('Version must have three numeric components')
    if value['architectures'] != ['arm64']:
        raise ValueError('This installer currently targets native arm64 only')
    if not re.fullmatch(r'\d+\.\d+(?:\.\d+)?', value['minimum_macos']):
        raise ValueError('Invalid minimum macOS version')
    return value

def inspect_bundle(bundle: Path, expected_version: str) -> Path:
    if bundle.is_symlink() or not bundle.is_dir():
        raise ValueError(f'Missing real plug-in bundle: {bundle}')
    with (bundle / 'Contents/Info.plist').open('rb') as stream:
        info = plistlib.load(stream)
    name = info.get('CFBundleExecutable', '')
    if not name or Path(name).name != name or name in ('.', '..'):
        raise ValueError(f'Invalid executable name in {bundle}')
    binary = bundle / 'Contents/MacOS' / name
    if binary.is_symlink() or not binary.is_file() or binary.stat().st_size < 4096:
        raise ValueError(f'Missing or suspiciously small plug-in executable: {binary}')
    # Header check before any signing/packaging; a text stub cannot become a pkg.
    with binary.open('rb') as stream:
        magic = stream.read(4)
    if magic not in (b'\xcf\xfa\xed\xfe', b'\xfe\xed\xfa\xcf', b'\xca\xfe\xba\xbe', b'\xbe\xba\xfe\xca', b'\xca\xfe\xba\xbf', b'\xbf\xba\xfe\xca'):
        raise ValueError(f'Executable is not a 64-bit Mach-O binary: {binary}')
    if info.get('CFBundleShortVersionString') != expected_version:
        raise ValueError(f'Bundle version does not match {expected_version}: {bundle}')
    if not info.get('CFBundleIdentifier', '').startswith('local.pitchworkbench.effect'):
        raise ValueError(f'Unexpected plug-in identity: {bundle}')
    return binary

def distribution_xml(version: str, minimum: str) -> bytes:
    doc = ET.Element('installer-gui-script', {'minSpecVersion': '2'})
    ET.SubElement(doc, 'title').text = f'Pitch Workbench {version}'
    ET.SubElement(doc, 'welcome', {'file': 'Welcome.html', 'mime-type': 'text/html'})
    ET.SubElement(doc, 'readme', {'file': 'ReadMe.html', 'mime-type': 'text/html'})
    ET.SubElement(doc, 'license', {'file': 'License.html', 'mime-type': 'text/html'})
    ET.SubElement(doc, 'conclusion', {'file': 'Conclusion.html', 'mime-type': 'text/html'})
    ET.SubElement(doc, 'options', {'customize': 'always', 'hostArchitectures': 'arm64', 'rootVolumeOnly': 'true'})
    ET.SubElement(doc, 'domains', {'enable_anywhere': 'false', 'enable_currentUserHome': 'false', 'enable_localSystem': 'true'})
    versions = ET.SubElement(doc, 'allowed-os-versions')
    ET.SubElement(versions, 'os-version', {'min': minimum})
    outline = ET.SubElement(doc, 'choices-outline')
    for kind in [*FORMATS, 'support']:
        ET.SubElement(outline, 'line', {'choice': kind})
        title = FORMATS[kind][3] if kind in FORMATS else 'Licenses, documentation and corresponding source'
        choice = ET.SubElement(doc, 'choice', {'id': kind, 'title': title, 'start_selected': 'true'})
        if kind == 'support':
            choice.set('visible', 'false'); choice.set('enabled', 'false')
        package_id = f'local.pitchworkbench.{kind}'
        ET.SubElement(choice, 'pkg-ref', {'id': package_id})
        ref = ET.SubElement(doc, 'pkg-ref', {'id': package_id, 'version': version, 'onConclusion': 'none'})
        ref.text = f'PitchWorkbench-{kind}.pkg'
    return ET.tostring(doc, encoding='utf-8', xml_declaration=True)

def add_tree(archive: zipfile.ZipFile, tree: Path, prefix: str, excluded: set[str]) -> None:
    for path in sorted(tree.rglob('*')):
        relative = path.relative_to(tree)
        if any(part in excluded for part in relative.parts):
            continue
        if path.is_symlink():
            raise ValueError(f'Source archive needs a regular file/directory, not a symlink: {path}')
        if path.is_file():
            archive.write(path, f'{prefix}/{relative.as_posix()}')

def source_archive(root: Path, build: Path, output: Path) -> None:
    vendors = [('JUCE', build / '_deps/juce-src'), ('rubberband', build / '_deps/rubberband-src')]
    # A rebuild from the shipped source may use local ThirdParty directories.
    vendors = [(name, root / 'ThirdParty' / name if (root / 'ThirdParty' / name).is_dir() else path) for name, path in vendors]
    for name, path in vendors:
        if not path.is_dir():
            raise ValueError(f'Corresponding dependency source is missing: {name}')
    with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        add_tree(archive, root, 'PitchWorkbench', {'build', 'dist', '.git', '__pycache__', 'ThirdParty'})
        for name, path in vendors:
            add_tree(archive, path, f'PitchWorkbench/ThirdParty/{name}', {'.git', '__pycache__'})

def component_info(path: Path) -> None:
    with path.open('rb') as stream:
        data = plistlib.load(stream)
    if not isinstance(data, list):
        raise ValueError('Unexpected component analysis result')
    def visit(items: list) -> None:
        for item in items:
            item['BundleIsRelocatable'] = False
            item['BundleOverwriteAction'] = 'upgrade'
            visit(item.get('ChildBundles', []))
    visit(data)
    with path.open('wb') as stream:
        plistlib.dump(data, stream)

def package(args: argparse.Namespace) -> Path:
    if sys.platform != 'darwin' or platform.machine() != 'arm64':
        raise RuntimeError('A native Apple silicon Mac is required to compile/package these plug-ins. No installer was created.')
    required = ['pkgbuild', 'productbuild', 'pkgutil', 'codesign', 'lipo', 'otool', 'ctest', 'installer']
    if args.release:
        required += ['xcrun', 'spctl']
        if not args.app_identity.startswith('Developer ID Application:') or not args.installer_identity.startswith('Developer ID Installer:') or not args.notary_profile:
            raise ValueError('Release needs Developer ID Application, Developer ID Installer, and a notarytool Keychain profile')
    for tool in required:
        if not shutil.which(tool):
            raise RuntimeError(f'Required Apple/build tool missing: {tool}')
    config = metadata(ROOT); version = config['version']
    build = Path(args.build).resolve()
    artefacts = build / 'PitchWorkbench_artefacts/Release'
    originals = {kind: artefacts / entry[0] / entry[1] for kind, entry in FORMATS.items()}
    for bundle in originals.values():
        inspect_bundle(bundle, version)
    registered = json.loads(run(['ctest', '--test-dir', build, '-C', 'Release', '--show-only=json-v1'], capture=True))
    tests = {test['name']: test for test in registered.get('tests', [])}
    for name in ('pitch_engine', 'processor_host', 'vst3_binary'):
        if name not in tests or any(p.get('name') == 'DISABLED' and p.get('value') for p in tests[name].get('properties', [])):
            raise ValueError(f'Required native test is missing or disabled: {name}')
    # A stale passing ctest log is insufficient: rerun the actual current tests.
    run(['ctest', '--test-dir', build, '-C', 'Release', '--output-on-failure', '--no-tests=error'])
    run(['bash', ROOT / 'scripts/validate-au.sh', originals['au'], build / 'validation'])
    destination = Path(args.output).resolve(); destination.mkdir(parents=True, exist_ok=True)
    suffix = '' if args.release else '-Local-Unsigned'
    target = destination / f'PitchWorkbench-{version}-macOS-arm64{suffix}.pkg'
    # Never leave a previous successful pkg at the output name after a failed
    # attempt, where it might be mistaken for the current build's result.
    if target.exists():
        raise FileExistsError(f'Output already exists; move it aside before rebuilding: {target}')
    with tempfile.TemporaryDirectory(prefix='pitchworkbench-package-') as temporary:
        work = Path(temporary); packages = work / 'packages'; packages.mkdir()
        manifest = {'version': version, 'architecture': 'arm64', 'minimum_macos': config['minimum_macos'],
                    'signed_and_notarized_release': bool(args.release), 'plugins': {}}
        for kind, entry in FORMATS.items():
            payload = work / f'{kind}-root'
            folder = payload / entry[2]; folder.mkdir(parents=True)
            bundle = folder / entry[1]
            shutil.copytree(originals[kind], bundle, symlinks=True)
            binary = inspect_bundle(bundle, version)
            run(['lipo', binary, '-verify_arch', 'arm64'])
            dependencies = run(['otool', '-L', binary], capture=True).splitlines()[1:]
            for line in dependencies:
                dependency = line.strip().split(' (', 1)[0]
                if dependency and not dependency.startswith(('/usr/lib/', '/System/Library/')):
                    raise ValueError(f'Plug-in is not self-contained; external dependency: {dependency}')
            signature = ['codesign', '--force', '--sign', args.app_identity if args.release else '-']
            if args.release: signature += ['--options', 'runtime', '--timestamp']
            run([*signature, bundle]); run(['codesign', '--verify', '--deep', '--strict', bundle])
            manifest['plugins'][kind] = {'executable_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
                                        'install_path': '/' + entry[2] + '/' + entry[1]}
            info = work / f'{kind}-components.plist'
            run(['pkgbuild', '--analyze', '--root', payload, info]); component_info(info)
            run(['pkgbuild', '--root', payload, '--component-plist', info,
                 '--identifier', f'local.pitchworkbench.{kind}', '--version', version,
                 '--install-location', '/', '--ownership', 'recommended',
                 '--scripts', ROOT / 'installer/scripts', packages / f'PitchWorkbench-{kind}.pkg'])
        support = work / 'support-root/Library/Application Support/Pitch Workbench'
        support.mkdir(parents=True)
        (support / 'Build-Manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        shutil.copytree(ROOT / 'docs', support / 'Documentation')
        shutil.copyfile(ROOT / 'README.md', support / 'README.md')
        shutil.copyfile(ROOT / 'LICENSE.txt', support / 'LICENSE-our-source.txt')
        source_archive(ROOT, build, support / f'PitchWorkbench-{version}-Corresponding-Source.zip')
        run(['pkgbuild', '--root', work / 'support-root', '--identifier', 'local.pitchworkbench.support',
             '--version', version, '--install-location', '/', '--ownership', 'recommended',
             packages / 'PitchWorkbench-support.pkg'])
        distribution = work / 'Distribution.xml'
        distribution.write_bytes(distribution_xml(version, config['minimum_macos']))
        staged = work / target.name
        command = ['productbuild', '--distribution', distribution, '--package-path', packages,
                   '--resources', ROOT / 'installer/resources']
        if args.release: command += ['--sign', args.installer_identity, '--timestamp']
        run([*command, staged])
        run(['pkgutil', '--expand', staged, work / 'expanded-product'])
        run(['installer', '-showChoicesXML', '-pkg', staged], capture=True)
        if args.release:
            run(['pkgutil', '--check-signature', staged])
            run(['xcrun', 'notarytool', 'submit', staged, '--keychain-profile', args.notary_profile, '--wait'])
            run(['xcrun', 'stapler', 'staple', staged]); run(['xcrun', 'stapler', 'validate', staged])
            run(['spctl', '--assess', '--verbose', '--type', 'install', staged])
        shutil.copy2(staged, target)
    digest = hashlib.sha256(target.read_bytes()).hexdigest()
    target.with_suffix('.pkg.sha256').write_text(f'{digest}  {target.name}\n')
    print(f'Created {target}')
    if not args.release: print('LOCAL DEVELOPMENT PACKAGE: unsigned; not notarized; not a public release.')
    return target

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', default=str(ROOT / 'build'))
    parser.add_argument('--output', default=str(ROOT / 'dist'))
    parser.add_argument('--release', action='store_true')
    parser.add_argument('--app-identity', default=os.environ.get('PW_APP_SIGNING_IDENTITY', ''))
    parser.add_argument('--installer-identity', default=os.environ.get('PW_INSTALLER_SIGNING_IDENTITY', ''))
    parser.add_argument('--notary-profile', default=os.environ.get('PW_NOTARY_PROFILE', ''))
    args = parser.parse_args()
    try:
        package(args)
    except (ValueError, RuntimeError, OSError, subprocess.CalledProcessError) as error:
        print(f'Packaging stopped: {error}', file=sys.stderr); return 1
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
