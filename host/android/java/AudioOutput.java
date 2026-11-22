package local.ios.host;

import android.media.AudioAttributes;
import android.media.AudioDeviceInfo;
import android.media.AudioFormat;
import android.media.AudioTrack;
import android.os.Handler;
import android.util.Log;
import java.io.File;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Files;
import java.util.concurrent.ConcurrentHashMap;

final class AudioOutput {
    private static native String finished(long token, boolean success);
    private static final int[] STEP = {7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,
        118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,
        1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,
        12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767};
    private static final int[] INDEX = {-1,-1,-1,-1,2,4,6,8};
    private static final class Sound {
        final int rate, channels, frames;
        final AudioTrack track;
        volatile boolean playing, paused;
        volatile int generation;
        float volume = 1; int loops, cursor;
        Sound(short[] samples, int rate, int channels) throws IOException {
            this.rate = rate; this.channels = channels; this.frames=samples.length/channels;
            if(frames==0) throw new IOException("Empty CAF audio data");
            int mask = channels == 1 ? AudioFormat.CHANNEL_OUT_MONO : AudioFormat.CHANNEL_OUT_STEREO;
            int minimum = AudioTrack.getMinBufferSize(rate, mask, AudioFormat.ENCODING_PCM_16BIT);
            if (minimum <= 0) throw new IOException("No Android PCM output for CAF format");
            track = new AudioTrack.Builder()
                .setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME).setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
                .setAudioFormat(new AudioFormat.Builder().setSampleRate(rate).setChannelMask(mask).setEncoding(AudioFormat.ENCODING_PCM_16BIT).build())
                .setBufferSizeInBytes(Math.max(samples.length*2,minimum)).setTransferMode(AudioTrack.MODE_STATIC).build();
            if (track.getState() == AudioTrack.STATE_UNINITIALIZED) { track.release(); throw new IOException("Android AudioTrack initialization failed"); }
            int written=track.write(samples,0,samples.length,AudioTrack.WRITE_BLOCKING);
            if(written!=samples.length || track.getState()!=AudioTrack.STATE_INITIALIZED) { track.release(); throw new IOException("Incomplete Android PCM upload"); }
        }
    }
    private final ConcurrentHashMap<Long, Sound> sounds = new ConcurrentHashMap<>();
    private final Handler main;
    private final HttpTransport.FailureSink failures;
    private volatile String lastError = "";
    AudioOutput(Handler main, HttpTransport.FailureSink failures) { this.main = main; this.failures = failures; }
    int outputCount() { return sounds.size(); }
    String lastError() { return lastError; }
    boolean anyPlaying() { for (Sound sound : sounds.values()) if (sound.playing && !sound.paused) return true; return false; }
    AudioDeviceInfo routedDevice() {
        for (Sound sound : sounds.values()) if (sound.playing && !sound.paused) {
            AudioDeviceInfo device = sound.track.getRoutedDevice();
            if (device != null) return device;
        }
        return null;
    }

    boolean prepare(long token, String filename) {
        try {
            byte[] file = Files.readAllBytes(new File(filename).toPath());
            ByteBuffer data = ByteBuffer.wrap(file).order(ByteOrder.BIG_ENDIAN);
            if (file.length < 8 || data.getInt() != 0x63616666 || data.getShort() != 1) throw new IOException("Expected CAF version 1");
            data.getShort();
            int rate = 0, channels = 0, packetBytes = 0, packetFrames = 0, audioStart = -1, audioBytes = 0;
            long validFrames = -1; int priming = 0;
            while (data.remaining() >= 12) {
                int tag = data.getInt(); long size = data.getLong(); int start = data.position();
                if (size == -1 && tag == 0x64617461) size = file.length - start;
                if (size < 0 || size > file.length - start) throw new IOException("Invalid CAF chunk length");
                if (tag == 0x64657363) {
                    if (size != 32) throw new IOException("Invalid CAF audio description");
                    double frequency = data.getDouble(); int format = data.getInt(); data.getInt();
                    packetBytes = data.getInt(); packetFrames = data.getInt(); channels = data.getInt(); data.getInt();
                    if (format != 0x696d6134 || frequency < 8000 || frequency > 192000 || frequency != Math.rint(frequency))
                        throw new IOException("Unsupported CAF encoding or sample rate");
                    rate = (int) frequency;
                } else if (tag == 0x70616b74) {
                    if (size < 24) throw new IOException("Invalid CAF packet table");
                    data.getLong(); validFrames = data.getLong(); priming = data.getInt(); data.getInt();
                } else if (tag == 0x64617461) {
                    if (size < 4) throw new IOException("Invalid CAF audio data");
                    audioStart = start + 4; audioBytes = (int) size - 4;
                }
                data.position(start + (int) size);
            }
            if (rate == 0 || channels < 1 || channels > 2 || packetBytes != 34 * channels || packetFrames != 64 || audioStart < 0 || audioBytes % packetBytes != 0)
                throw new IOException("Unsupported CAF IMA4 packet layout");
            int packets = audioBytes / packetBytes;
            long available = (long) packets * 64 - priming;
            if (validFrames < 0) validFrames = available;
            if (priming < 0 || validFrames < 0 || validFrames > available || validFrames * channels > 64 * 1024 * 1024)
                throw new IOException("Invalid CAF frame count");
            short[] pcm = new short[(int) validFrames * channels];
            int[] predictor = new int[channels], index = new int[channels];

            for (int packet = 0; packet < packets; packet++) {
                for (int channel = 0; channel < channels; channel++) {
                    int offset = audioStart + packet * packetBytes + channel * 34;
                    int header = (short) (((file[offset] & 255) << 8) | (file[offset + 1] & 255));
                    int nextIndex = header & 127, nextPredictor = header & ~127;
                    if (nextIndex > 88) throw new IOException("Invalid IMA4 step index");
                    if (index[channel] != nextIndex || Math.abs(predictor[channel] - nextPredictor) > 127) predictor[channel] = nextPredictor;
                    index[channel] = nextIndex;
                    for (int frame = 0; frame < 64; frame++) {
                        int nibble = ((file[offset + 2 + frame / 2] & 255) >>> ((frame & 1) * 4)) & 15;
                        int step = STEP[index[channel]], magnitude = step >>> 3;
                        if ((nibble & 1) != 0) magnitude += step >>> 2;
                        if ((nibble & 2) != 0) magnitude += step >>> 1;
                        if ((nibble & 4) != 0) magnitude += step;
                        predictor[channel] = Math.max(-32768, Math.min(32767, predictor[channel] + ((nibble & 8) == 0 ? magnitude : -magnitude)));
                        index[channel] = Math.max(0, Math.min(88, index[channel] + INDEX[nibble & 7]));
                        long sample = (long) packet * 64 + frame - priming;
                        if (sample >= 0 && sample < validFrames) pcm[(int) sample * channels + channel] = (short) predictor[channel];
                    }
                }
            }
            Sound sound = new Sound(pcm, rate, channels);
            Sound previous=sounds.put(token,sound); if(previous!=null) release(previous);
            lastError="";
            Log.i("I2AGuest", "CAF IMA4 prepared: " + rate + " Hz, " + channels + " channels, " + validFrames + " frames");
            return true;
        } catch (IOException | IllegalArgumentException | UnsupportedOperationException error) {
            lastError = error.toString(); Log.e("I2AGuest", "CAF preparation failed", error); return false;
        }
    }

    private boolean arm(long token,Sound sound) {
        long end=(long)sound.frames*(sound.loops+1L);
        if(sound.loops>=0 && end>Integer.MAX_VALUE) { lastError="Playback loop count exceeds the Android marker range"; return false; }
        int generation=++sound.generation;
        sound.track.setPlaybackPositionUpdateListener(new AudioTrack.OnPlaybackPositionUpdateListener() {
            @Override public void onPeriodicNotification(AudioTrack track) {}
            @Override public void onMarkerReached(AudioTrack track) {
                synchronized(sound) {
                    if(sounds.get(token)!=sound || sound.generation!=generation || !sound.playing || sound.paused || sound.loops<0 ||
                        Integer.toUnsignedLong(track.getPlaybackHeadPosition())<end) return;
                    sound.playing=false; sound.cursor=0; track.stop();
                }
                String failure=finished(token,true); if(failure!=null) failures.report(failure);
            }
        },main);
        int loop=sound.track.setLoopPoints(0,sound.frames,sound.loops);
        int marker=sound.track.setNotificationMarkerPosition(sound.loops<0 ? 0 : (int)end);
        if(loop!=AudioTrack.SUCCESS || marker!=AudioTrack.SUCCESS) { lastError="Unable to configure Android playback loops or marker"; return false; }
        return true;
    }
    private boolean fail(Sound sound,String message) {
        lastError=message; ++sound.generation; sound.playing=false; sound.paused=false;
        sound.track.setPlaybackPositionUpdateListener(null); sound.track.stop(); return false;
    }
    private void finishAtEnd(long token,Sound sound) {
        int generation=++sound.generation; sound.track.stop();
        main.post(()-> {
            synchronized(sound) {
                if(sounds.get(token)!=sound || sound.generation!=generation || !sound.playing || sound.paused) return;
                sound.playing=false; sound.cursor=0;
            }
            String failure=finished(token,true); if(failure!=null) failures.report(failure);
        });
    }
    boolean play(long token) {
        Sound sound = sounds.get(token);
        if (sound == null) return false;
        synchronized (sound) {
            try {
                if(sound.playing && !sound.paused) return true;
                if(sound.cursor==sound.frames && sound.loops==0) { sound.playing=true; sound.paused=false; finishAtEnd(token,sound); return true; }
                if(!sound.playing) {
                    sound.track.stop();
                    if(sound.track.reloadStaticData()!=AudioTrack.SUCCESS || sound.track.setPlaybackHeadPosition(sound.cursor)!=AudioTrack.SUCCESS) return fail(sound,"Unable to restore Android playback position");
                    if(!arm(token,sound)) return fail(sound,lastError);
                }
                sound.playing=true; sound.paused=false; sound.track.play(); return true;
            } catch(IllegalStateException | IllegalArgumentException error) { return fail(sound,error.toString()); }
        }
    }
    void pause(long token) { Sound sound=sounds.get(token); if(sound!=null) synchronized(sound) { if(sound.playing) { sound.paused=true; sound.track.pause(); } } }
    void stop(long token) {
        Sound sound = sounds.get(token);
        if (sound == null) return;
        synchronized(sound) { if(sound.playing) sound.cursor=(int)(Integer.toUnsignedLong(sound.track.getPlaybackHeadPosition())%sound.frames); ++sound.generation; sound.playing=false; sound.paused=false; sound.track.stop(); }
    }
    void volume(long token, float value) {
        Sound sound = sounds.get(token);
        if (sound != null) { sound.volume = Math.max(0, Math.min(1, value)); sound.track.setVolume(sound.volume); }
    }
    double property(long token, int property) {
        Sound sound = sounds.get(token);
        if (sound == null) return 0;
        if (property == 0) return (double) sound.frames / sound.rate;
        if (property == 1) { long head=sound.playing ? Integer.toUnsignedLong(sound.track.getPlaybackHeadPosition()) : sound.cursor; return (sound.loops==0 ? Math.min(head,sound.frames) : head%sound.frames)/(double)sound.rate; }
        if (property == 2) return sound.playing && !sound.paused ? 1 : 0;
        if (property == 3) return sound.volume;
        if(property==4) return sound.channels;
        return sound.loops;
    }
    boolean seek(long token,double seconds) {
        Sound sound=sounds.get(token); if(sound==null || !Double.isFinite(seconds) || seconds<0 || seconds>sound.frames/(double)sound.rate) return false;
        synchronized(sound) {
            try {
                boolean resume=sound.playing && !sound.paused;
                if(sound.playing) sound.track.pause();
                sound.cursor=Math.min(sound.frames,(int)Math.round(seconds*sound.rate));
                if(sound.cursor==sound.frames && sound.loops==0 && resume) { finishAtEnd(token,sound); return true; }
                if(sound.track.setPlaybackHeadPosition(sound.cursor)!=AudioTrack.SUCCESS) return fail(sound,"Unable to seek Android audio");
                if(!arm(token,sound)) return fail(sound,lastError);
                if(resume) sound.track.play(); return true;
            } catch(IllegalStateException | IllegalArgumentException error) { return fail(sound,error.toString()); }
        }
    }
    boolean loops(long token,int value) {
        Sound sound=sounds.get(token); if(sound==null || value < -1) return false;
        synchronized(sound) {
            if(value>=0 && (long)sound.frames*(value+1L)>Integer.MAX_VALUE) { lastError="Playback loop count exceeds the Android marker range"; return false; }
            try {
                boolean resume=sound.playing && !sound.paused;
                if(sound.playing) { sound.track.pause(); sound.cursor=(int)(Integer.toUnsignedLong(sound.track.getPlaybackHeadPosition())%sound.frames); }
                if(sound.track.setPlaybackHeadPosition(sound.cursor)!=AudioTrack.SUCCESS) return fail(sound,"Unable to reset Android loop position");
                sound.loops=value;
                if(!arm(token,sound)) return fail(sound,lastError);
                if(resume) sound.track.play(); return true;
            } catch(IllegalStateException | IllegalArgumentException error) { return fail(sound,error.toString()); }
        }
    }
    private void release(Sound sound) { synchronized(sound) { ++sound.generation; sound.playing=false; sound.track.setPlaybackPositionUpdateListener(null); sound.track.release(); } }
    boolean dispose(long token) { Sound sound=sounds.remove(token); if(sound==null) return false; release(sound); return true; }
    void close() { for(Sound sound:sounds.values()) release(sound); sounds.clear(); }
}
