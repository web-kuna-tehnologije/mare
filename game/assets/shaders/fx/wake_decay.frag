// The wake map's fade (gfx/wake.cc): transparent black over the whole map. With the alpha
// blend that is dst = dst * (1 - u_fade), i.e. an exponential decay at the caller's time
// constant - the trail a ship left behind her thinning out over a minute or two.
uniform float u_fade;
out vec4 fragColor;
void main(){ fragColor = vec4(0.0, 0.0, 0.0, clamp(u_fade, 0.0, 1.0)); }
