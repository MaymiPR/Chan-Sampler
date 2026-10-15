"""In-FL null cases for FUNC > Echo delay. Event 209 (5 x u32): feedback
(raw / 25600), pan (centre 6400), pitch (cents), echoes, time (raw / 48 steps)."""
import sys, struct, flrender as F, nullfl

def echo(name, feed=12800, pan=6400, pitch=0, count=4, time=144, sample='decay.wav', beats=2):
    def patch(ev):
        for _, e in F._channel0(ev):
            if e[0] == 209:
                struct.pack_into('<5I', e[1], 0, feed, pan, pitch & 0xffffffff, count, time); return
        raise KeyError(209)
    ours = {'echoFeed': feed / 25600, 'echoPan': (pan - 6400) / 6400, 'echoPitch': pitch,
            'echoCount': count, 'echoTime': time / 48}
    return dict(name=name, sample=sample, fl=[patch], ours=ours, beats=beats, notes=[(0, beats, 60, 100)])

CASES = [echo('ec_base'), echo('ec_feed25', feed=6400), echo('ec_feed100', feed=25600), echo('ec_count1', count=1),
         echo('ec_count8', count=8), echo('ec_time48', time=48), echo('ec_time100', time=100),
         echo('ec_panL', pan=3200), echo('ec_panR', pan=12800), echo('ec_pitch100', pitch=100), echo('ec_pitchm300', pitch=-300)]

if __name__ == '__main__':
    nullfl.run(CASES, [a for a in sys.argv[1:] if a != '-f'] or None, force='-f' in sys.argv)
