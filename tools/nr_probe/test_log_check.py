import unittest
from check_dlss_log import check

class LogChecks(unittest.TestCase):
    def trace(self):
        lines=['DLSS: ready','DLSS: AUDIT create input=1280x720 output=1920x1080 quality=1 hdr=0 depth_inverted=0 flags=0x2']
        for frame in range(1,9):
            lines.append(f'DLSS: AUDIT frame={frame} color=1280x736:37 depth=1280x736:130 mv=1280x720:83 output=1920x1080:97 jitter={frame/32:.6f},-0.166667 mv_scale=1,1 reset={int(frame==1)} frame_ms=16.667 result=0x00000001')
        return '\n'.join(lines)

    def test_aligned_scene_subrectangle(self):
        self.assertEqual(check(self.trace(),'quality'),8)

    def test_rejects_false_success(self):
        mutations={
            'E_FALLBACK_OR_RUNTIME_ERROR':lambda t:t+'\nUpscaler: DLSS failed; falling back to FSR 3.1',
            'E_RESOURCE_EXTENT':lambda t:t.replace('mv=1280x720','mv=640x360'),
            'E_MODE':lambda t:t.replace('quality=1','quality=0'),
            'E_NGX_RESULT':lambda t:t.replace('result=0x00000001','result=0xbad00001'),
            'E_RESET_MISSING':lambda t:t.replace('reset=1','reset=0'),
            'E_FRAME_METADATA':lambda t:t.replace('mv_scale=1,1','mv_scale=0,1'),
            'E_FRAMES_MISSING':lambda t:t.split('DLSS: AUDIT frame=2')[0],
            'E_READY_MISSING':lambda t:t.replace('DLSS: ready',''),
            'E_DEPTH_OR_MV_FLAGS':lambda t:t.replace('flags=0x2','flags=0x6'),
        }
        for code,mutate in mutations.items():
            with self.subTest(code=code), self.assertRaisesRegex(ValueError,code):
                check(mutate(self.trace()),'quality')

if __name__=='__main__':unittest.main()
