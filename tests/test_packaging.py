import importlib.util
import json
from pathlib import Path
import plistlib
import tempfile
import unittest
import xml.etree.ElementTree as ET
import zipfile

MODULE = Path(__file__).resolve().parents[1] / 'scripts/package_macos.py'
spec = importlib.util.spec_from_file_location('package_macos', MODULE)
packaging = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packaging)

class PackagingTests(unittest.TestCase):
    """Portable manifest/metadata tests. These do NOT execute Apple tools."""
    def test_distribution_has_optional_au_vst3_and_mandatory_source(self):
        doc = ET.fromstring(packaging.distribution_xml('0.2.0', '12.0'))
        self.assertEqual(doc.find('options').get('hostArchitectures'), 'arm64')
        self.assertEqual(doc.find('allowed-os-versions/os-version').get('min'), '12.0')
        self.assertEqual(doc.find('domains').get('enable_localSystem'), 'true')
        choices = {c.get('id'): c for c in doc.findall('choice')}
        self.assertEqual(set(choices), {'au', 'vst3', 'support'})
        for name in ('au', 'vst3'):
            self.assertEqual(choices[name].get('start_selected'), 'true')
            self.assertIsNone(choices[name].get('enabled'))
        self.assertEqual(choices['support'].get('enabled'), 'false')
        self.assertEqual(choices['support'].get('start_selected'), 'true')
        top_refs = {p.get('id'): p.text for p in doc.findall('pkg-ref')}
        self.assertEqual(len(top_refs), 3)
        for choice in choices.values():
            self.assertIn(choice.find('pkg-ref').get('id'), top_refs)

    def test_system_install_destinations_are_standard(self):
        self.assertEqual(packaging.FORMATS['au'][2], 'Library/Audio/Plug-Ins/Components')
        self.assertEqual(packaging.FORMATS['vst3'][2], 'Library/Audio/Plug-Ins/VST3')

    def test_metadata_validates_version_and_architecture(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); meta = root / 'version.json'
            value = dict(version='0.2.0', minimum_macos='12.0', architectures=['arm64'])
            meta.write_text(json.dumps(value)); self.assertEqual(packaging.metadata(root)['version'], '0.2.0')
            value['version'] = '../../fake.pkg'; meta.write_text(json.dumps(value))
            with self.assertRaises(ValueError): packaging.metadata(root)
            value['version'] = '0.2.0'; value['architectures'] = ['x86_64']; meta.write_text(json.dumps(value))
            with self.assertRaises(ValueError): packaging.metadata(root)

    def test_bundle_inspection_rejects_missing_text_wrong_version_and_traversal(self):
        with tempfile.TemporaryDirectory() as temp:
            bundle = Path(temp) / 'Effect.vst3'
            with self.assertRaises(ValueError): packaging.inspect_bundle(bundle, '0.2.0')
            macos = bundle / 'Contents/MacOS'; macos.mkdir(parents=True)
            info_path = bundle / 'Contents/Info.plist'
            info = dict(CFBundleExecutable='Effect', CFBundleShortVersionString='0.2.0', CFBundleIdentifier='local.pitchworkbench.effect.vst3')
            def save(): info_path.write_bytes(plistlib.dumps(info))
            save(); binary = macos / 'Effect'; binary.write_bytes(b'not a binary' * 500)
            with self.assertRaises(ValueError): packaging.inspect_bundle(bundle, '0.2.0')
            # This fixture tests metadata inspection only. lipo, codesign and
            # actual native loading remain mandatory Mac gates.
            binary.write_bytes(b'\xcf\xfa\xed\xfe' + b'0' * 5000)
            self.assertEqual(packaging.inspect_bundle(bundle, '0.2.0'), binary)
            info['CFBundleExecutable'] = '../Effect'; save()
            with self.assertRaises(ValueError): packaging.inspect_bundle(bundle, '0.2.0')
            info['CFBundleExecutable'] = 'Effect'; info['CFBundleShortVersionString'] = '0.1.0'; save()
            with self.assertRaises(ValueError): packaging.inspect_bundle(bundle, '0.2.0')

    def test_component_bundles_are_not_relocatable(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'info.plist'
            path.write_bytes(plistlib.dumps([dict(BundleIsRelocatable=True, ChildBundles=[{}])]))
            packaging.component_info(path)
            result = plistlib.loads(path.read_bytes())[0]
            self.assertFalse(result['BundleIsRelocatable'])
            self.assertEqual(result['BundleOverwriteAction'], 'upgrade')
            self.assertFalse(result['ChildBundles'][0]['BundleIsRelocatable'])

    def test_corresponding_source_includes_dependencies_and_excludes_build(self):
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp); root = base / 'project'; build = root / 'build'
            root.mkdir(); (root / 'CMakeLists.txt').write_text('project(example)')
            for name in ('juce-src', 'rubberband-src'):
                path = build / '_deps' / name; path.mkdir(parents=True)
                (path / 'LICENSE').write_text('fixture license')
            (build / 'not-source.bin').write_bytes(b'not source')
            archive = base / 'source.zip'; packaging.source_archive(root, build, archive)
            with zipfile.ZipFile(archive) as z:
                names = set(z.namelist())
                self.assertIn('PitchWorkbench/ThirdParty/JUCE/LICENSE', names)
                self.assertIn('PitchWorkbench/ThirdParty/rubberband/LICENSE', names)
                self.assertIn('PitchWorkbench/CMakeLists.txt', names)
                self.assertFalse(any('/build/' in n for n in names))

    def test_source_archive_refuses_missing_dependency_source(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            with self.assertRaises(ValueError): packaging.source_archive(root, root / 'build', root / 'source.zip')

    def test_source_archive_refuses_symlinks(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); (root / 'actual').write_text('test')
            (root / 'link').symlink_to(root / 'actual')
            with zipfile.ZipFile(root / 'output.zip', 'w') as z:
                with self.assertRaises(ValueError): packaging.add_tree(z, root, 'root', {'output.zip'})

if __name__ == '__main__':
    unittest.main()
