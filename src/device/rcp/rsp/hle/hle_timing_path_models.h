/* Complete native tasks; nonnegative work/path coefficients in Q8. */
static const unsigned timing_path_kinds[8] = { HLE_PATH_TRI_CLIP, HLE_PATH_CLIP_VERTEX, HLE_PATH_CLIP_NEG_DENOM, HLE_PATH_TRI_TEXTURE, HLE_PATH_TRI_SHADE, HLE_PATH_TRI_DEPTH, HLE_PATH_CULL_FIRST, HLE_PATH_TRI_FLAT };
static const struct timing_path_model timing_path_models[] = {
    { "gfx_f3d", 0x234f3u, 3156394u, { 12303u, 256u, 1024u, 23040u, 10240u, 14336u, 2048u, 12288u, 30209u, 19299u, 82467u, 384000u, 128000u, 307200u, 8192u, 563200u, 3413u, 1109u, 151414u, 4096u, 0u }, { 90355u, 5852u, 12982u, 9727u, 0u, 0u, 0u, 26411u } },
    { "gfx_f3dex2", 0x28c00u, 2498552u, { 5363u, 256u, 1024u, 23040u, 10240u, 14336u, 2048u, 12288u, 14865u, 26379u, 45975u, 384000u, 128000u, 307200u, 8192u, 563200u, 1124u, 860u, 38546u, 4096u, 1024u }, { 171929u, 0u, 0u, 0u, 0u, 0u, 0u, 0u } },
};
