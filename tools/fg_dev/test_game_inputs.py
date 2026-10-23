import unittest
import numpy as np
from prepare_game_inputs import matrices


class GameCameraTests(unittest.TestCase):
    def frame(self):
        return dict(near=.1, far=1000., vertical_fov=np.pi/2,
                    width=1920, height=1080, camera_position=[3.,4.,5.],
                    camera_right=[1.,0.,0.], camera_up=[0.,1.,0.], camera_forward=[0.,0.,1.])

    def test_near_far_and_camera_centre(self):
        projection, view = matrices(self.frame())
        for z, expected in [(.1,0.),(1000.,1.)]:
            clip = np.array([3.,4.,5.+z,1.]) @ view @ projection
            np.testing.assert_allclose((clip/clip[3])[:3], [0,0,expected], atol=1e-12)

    def test_rotation_and_temporal_reprojection(self):
        previous = self.frame()
        current = self.frame()
        current.update(camera_right=[0.,0.,-1.], camera_forward=[1.,0.,0.])
        pp, pv = matrices(previous)
        cp, cv = matrices(current)
        world = np.array([7.,6.,13.,1.])
        to_previous = np.linalg.inv(cv @ cp) @ pv @ pp
        np.testing.assert_allclose(world @ cv @ cp @ to_previous, world @ pv @ pp, atol=1e-12)

    def test_invalid_basis_rejected(self):
        frame = self.frame();frame['camera_up']=[0.,0.,1.]
        with self.assertRaisesRegex(ValueError,'orthonormal'):
            matrices(frame)


if __name__ == '__main__':
    unittest.main()
