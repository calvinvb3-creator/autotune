"""Measure exported waveforms with an FFT independent of the plug-in detector."""
import json
import sys
import wave
from pathlib import Path
import numpy as np
root=Path(sys.argv[1] if len(sys.argv)>1 else 'build')
def load(name):
    with wave.open(str(root/name),'rb') as f:
        rate=f.getframerate(); assert f.getsampwidth()==2
        return rate,np.frombuffer(f.readframes(f.getnframes()),dtype='<i2').astype(float)/32768

def hz(samples,rate):
    samples=np.asarray(samples);n=1<<int(np.ceil(np.log2(len(samples)*8)))
    amplitude=np.abs(np.fft.rfft(samples*np.hanning(len(samples)),n))
    low=int(60*n/rate);high=int(1000*n/rate)
    k=int(np.argmax(amplitude[low:high]))+low
    a,b,c=np.log(np.maximum(amplitude[k-1:k+2],1e-20));offset=0.5*(a-c)/(a-2*b+c)
    return (k+offset)*rate/n
rate,source=load('offline-source.wav');_,edited=load('offline-moved-stretched-C4.wav');_,quieter=load('offline-gain-minus6.wav')
actual=hz(edited[int(1.7*rate):int(3.2*rate)],rate);expected=440*2**(-9/12)
cents=1200*np.log2(actual/expected);assert abs(cents)<15, cents
old=np.sqrt(np.mean(edited[int(.65*rate):int(.85*rate)]**2));assert old<1e-5,old
assert np.sqrt(np.mean(edited[int(3.2*rate):int(3.6*rate)]**2))>.06
window=slice(int(rate),int(2*rate));db=20*np.log10(np.sqrt(np.mean(quieter[window]**2))/np.sqrt(np.mean(source[window]**2)));assert abs(db+6)<.1,db
_,modulated=load('offline-vibrato-source.wav');_,flattened=load('offline-flattened.wav')
# FFT windows measure centre drift, while a Hilbert phase trace measures vibrato.
from scipy.signal import hilbert, butter, sosfiltfilt
sos=butter(3,[120,350],btype='bandpass',fs=rate,output='sos')
def spread(x):
    filtered=sosfiltfilt(sos,x)
    phase=np.unwrap(np.angle(hilbert(filtered)))
    f=np.diff(phase)*rate/(2*np.pi)
    f=f[int(rate):int(2.5*rate)]
    return float(np.std(1200*np.log2(np.maximum(f,1)/220)))
before=spread(modulated);after=spread(flattened);assert after<before*.65,(before,after)
result=dict(fft_pitch_cents_error=float(cents),moved_source_rms=float(old),gain_db=float(db),pitch_variation_before_cents=before,pitch_variation_after_cents=after)
print(json.dumps(result,indent=2))
(root/'validation').mkdir(exist_ok=True)
(root/'validation/offline-independent-audio.json').write_text(json.dumps(result,indent=2)+'\n')
