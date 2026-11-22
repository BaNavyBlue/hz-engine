// (build 107) The CRT chain's first pass: the beams down.
//
// Target: 320 x the output's height, RGBA32 float. At source column i and
// display row y, the light of lines jmid-3 .. jmid+3 (jmid the line row y
// is in): for each gun, the Beam table's value at the gun's DAC level and
// the row's offset from the line's top -- the level times its beam's share
// of that row, worked out on the CPU by crt.c (crt_tables_build) from the
// model's own functions. src/core/crt.c's down_texel is this, in C.
//
// Resources, as SDL_GPU wants them for a fragment shader: sampled textures
// and their samplers in space2, the uniform buffer in space3.

Texture2D<float4> Frame   : register(t0, space2); // 320x224 R8: index / 255
SamplerState      FrameS  : register(s0, space2);
Texture2D<float4> Palette : register(t1, space2); // 80x1 RGBA8: DAC levels / 255
SamplerState      PaletteS: register(s1, space2);
Texture2D<float4> Beam    : register(t2, space2); // 7k x 8 R32F, by level
SamplerState      BeamS   : register(s2, space2);

cbuffer Down : register(b0, space3)
{
    int pic_y;      // the picture's top, in display rows
    int k;          // display rows a line
    int reach;      // lines either side (3)
    int lines;      // 224
};

int floor_div(int a, int b)
{
    return (a >= 0) ? a / b : -((-a + b - 1) / b);
}

float4 main(float4 position : SV_Position) : SV_Target0
{
    int i    = (int)position.x;
    int y    = (int)position.y;
    int r0   = y - pic_y;
    int jmid = floor_div(r0, k);

    float3 acc = float3(0.0, 0.0, 0.0);
    for (int dj = -reach; dj <= reach; dj++) {
        int j = jmid + dj;
        if (j >= 0 && j < lines) {
            int  col = r0 - j * k + reach * k;
            int  idx = (int)(Frame.Load(int3(i, j, 0)).r * 255.0 + 0.5);
            int3 lv  = (int3)(Palette.Load(int3(idx, 0, 0)).rgb * 255.0
                              + 0.5);
            acc.r += Beam.Load(int3(col, lv.r, 0)).r;
            acc.g += Beam.Load(int3(col, lv.g, 0)).r;
            acc.b += Beam.Load(int3(col, lv.b, 0)).r;
        }
    }
    return float4(acc, 1.0);
}
