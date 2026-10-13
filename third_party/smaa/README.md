# SMAA dependency

Unmodified `SMAA.hlsl`, `Textures/AreaTex.h`, `Textures/SearchTex.h` and `LICENSE.txt`
from https://github.com/iryoku/smaa at commit `71c806a838bdd7d517df19192a20f0c61b3ca29d`.

The shader wrappers use the D3D9 High preset. Area and search lookups are embedded
as C++ data, uploaded as A8L8 and L8 respectively, and sampled without sRGB decoding.
See `LICENSE.txt` for the MIT license and binary-distribution clarification.
