/* camera_area_at: shapes, rotation, last match wins. */
#include <assert.h>
#include <stdio.h>
#include "../src/camera.h"

int main(void)
{
    CamArea a[3] = {
        {0, 1, 0, {0, 0, 0}, {0, 0, 0}, {2, 2, 2}},      /* box: |x|, |y|, |z| < 1000 */
        {1, 1, 1, {0, 0, 0}, {0, 0, 90}, {1, 4, 1}},     /* cylinder rotated 90 about Z: axis -X, 0..2000 long, r 500 */
        {2, 1, 2, {5000, 0, 0}, {0, 0, 0}, {1, 1, 1}},   /* sphere r 500 at x 5000 */
    };
    assert(camera_area_at(a, 3, v3(900, 0, 0)) == 0);
    assert(camera_area_at(a, 3, v3(-900, 0, 0)) == 1);   /* inside both: the last one wins */
    assert(camera_area_at(a, 3, v3(-1500, 100, 0)) == 1);
    assert(camera_area_at(a, 3, v3(1500, 0, 0)) == -1);
    assert(camera_area_at(a, 3, v3(5300, 0, 300)) == 2);
    assert(camera_area_at(a, 3, v3(0, 0, 0)) == 1);       /* cylinder base on the origin: h = 0 is inside */
    printf("test_camera ok\n");
    return 0;
}
