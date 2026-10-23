// SPDX-License-Identifier: GPL-3.0-or-later
// Finite synthetic or recorded Bloodborne fixture. Never a live Present test.
void Probe::evaluate(const char *run) {
  require(snippet && bridge && feature,
          "Evaluate needs a created direct FG feature");
  auto vt = *reinterpret_cast<void ***>(params);
  auto ui = [&](const char *k, unsigned v) {
    reinterpret_cast<void(__thiscall *)(void *, const char *, unsigned)>(vt[3])(
        params, k, v);
  };
  auto ull = [&](const char *k, unsigned long long v) {
    reinterpret_cast<void(__thiscall *)(
        void *, const char *, unsigned long long)>(vt[0])(params, k, v);
  };
  auto ptr = [&](const char *k, const void *p) {
    ull(k, reinterpret_cast<unsigned long long>(p));
  };
  auto fp = [&](const char *k, float v) {
    reinterpret_cast<void(__thiscall *)(void *, const char *, float)>(vt[6])(
        params, k, v);
  };
  // Slot 6 is independently proven for this pinned driver block by the NR
  // fixture. Verify its exact width/round-trip again before writing FG float
  // parameters.
  for (float expected : {.375f, .625f}) {
    alignas(16) unsigned char storage[64];
    memset(storage, 0xa5, sizeof storage);
    fp("bbport.FG.FloatProbe", expected);
    int r = reinterpret_cast<int(__thiscall *)(void *, const char *, void *)>(
        vt[14])(params, "bbport.FG.FloatProbe", storage + 16);
    float value = 0;
    memcpy(&value, storage + 16, 4);
    for (unsigned j = 0; j < 64; ++j)
      require((j >= 16 && j < 20) || storage[j] == 0xa5,
              "float ABI canary/width");
    require(r == 1 && value == expected, "float ABI exact roundtrip");
  }
  puts("FG_FLOAT_ABI setter=6 getter=14 exact_roundtrips=2 canary=1 "
       "float_width=1");
  ull("bbport.FG.PointerProbe", 0x1234567812345678ull);
  alignas(16) unsigned char storage[64];
  memset(storage, 0xa5, sizeof storage);
  int pr = reinterpret_cast<int(__thiscall *)(void *, const char *, void *)>(
      vt[8])(params, "bbport.FG.PointerProbe", storage + 16);
  unsigned long long pv = 0;
  memcpy(&pv, storage + 16, 8);
  for (unsigned j = 0; j < 64; ++j)
    require((j >= 16 && j < 24) || storage[j] == 0xa5, "pointer ABI canary");
  require(pr == 1 && pv == 0x1234567812345678ull, "pointer ABI roundtrip");
  nlohmann::json game;
  const char* input_path=getenv("FG_POC_INPUT_MANIFEST");
  if(input_path){std::ifstream file(win(input_path));require(bool(file),"game input manifest open");file>>game;
    require(game.at("schema")=="dlssg-game-input-fixture-v1"&&game.at("frames").size()==6,"finite game fixture schema");
    puts("FG_INPUT_SCOPE captured_bloodborne=1 native_presents=0");}
  const unsigned RW=input_path?1280:1920,RH=input_path?720:1080;
  fixture =
      std::make_unique<FgFixture>(device, physical, command, gdpa,
                                  i<PFN_vkGetPhysicalDeviceMemoryProperties>(
                                      "vkGetPhysicalDeviceMemoryProperties"));
  auto &g = *fixture;
  constexpr auto W = FgFixture::W, H = FgFixture::H;
  constexpr size_t pixels = size_t(W) * H;
  struct Frame {
    FgFixture::Image color, depth, motion, output;
    FgFixture::Buffer color_upload, depth_upload, motion_upload, readback,
        disabled;
  };
  auto descriptors=std::make_shared<std::vector<Frame>>();
  input_descriptors=descriptors;auto& frames=*descriptors;
  frames.reserve(6);
  const bool reuse_inputs=getenv("FG_POC_REUSE_INPUTS")!=nullptr;
  for (unsigned n = 0; n < 6; ++n) {
    Frame f{reuse_inputs&&n>=2?frames[n%2].color:g.image(VK_FORMAT_R8G8B8A8_UNORM, false),
            reuse_inputs&&n>=2?frames[n%2].depth:g.image(VK_FORMAT_R32_SFLOAT, false,RW,RH),
            reuse_inputs&&n>=2?frames[n%2].motion:g.image(VK_FORMAT_R32G32_SFLOAT, false,RW,RH),
            g.image(VK_FORMAT_R8G8B8A8_UNORM, true),
            g.buffer(pixels * 4),
            g.buffer(size_t(RW)*RH * 4),
            g.buffer(size_t(RW)*RH * 8),
            g.buffer(pixels * 4),
            g.buffer(4)};
    auto *color = static_cast<unsigned char *>(f.color_upload.mapped);
    auto *depth = static_cast<float *>(f.depth_upload.mapped);
    auto *motion = static_cast<float *>(f.motion_upload.mapped);
    if(input_path){const auto& metadata=game.at("frames").at(n);
      auto read=[&](const char* role,void* target,size_t bytes){std::string path=metadata.at("files").at(role).at("path");FILE* file=fopen(win(path).c_str(),"rb");require(file,"game raw open");bool ok=fread(target,1,bytes,file)==bytes;ok=fgetc(file)==EOF&&ok;ok=fclose(file)==0&&ok;require(ok,"game bounded raw read");};
      read("color",color,pixels*4);read("depth",depth,size_t(RW)*RH*4);
      std::vector<uint16_t> half(size_t(RW)*RH*2);read("motion",half.data(),half.size()*2);
      for(size_t j=0;j<half.size();++j){unsigned h=half[j],e=(h>>10)&31,m=h&1023;require(e!=31,"finite half motion");float v=e?std::ldexp(1.f+float(m)/1024,int(e)-15):std::ldexp(float(m),-24);motion[j]=(h&32768)?-v:v;}
      if(metadata.at("color_format")==44)for(size_t j=0;j<pixels;++j)std::swap(color[j*4],color[j*4+2]);
      frames.push_back(f);continue;
    }
    const unsigned left = 300 + n * 32;
    for (unsigned y = 0; y < H; ++y)
      for (unsigned x = 0; x < W; ++x) {
        const size_t p = size_t(y) * W + x;
        const bool object = x >= left && x < left + 400 && y >= 300 && y < 680;
        const bool stripe = object && ((x - left) / 32 % 2);
        color[p * 4] = object ? (stripe ? 245 : 210)
                              : unsigned(20 + (x / 64 + y / 64) % 2 * 15);
        color[p * 4 + 1] = object ? 55 : 42;
        color[p * 4 + 2] = object ? 75 : 62;
        color[p * 4 + 3] = 255;
        depth[p] = object ? .35f : .9f;
        motion[p * 2] = object ? -32.f : 0.f;
        motion[p * 2 + 1] = 0.f;
      }
    frames.push_back(f);
  }
  float projection[16]{}, inverse[16]{}, identity[16]{};
  const float q = 1.f / std::tan(1.0471975512f / 2), a = q / (float(W) / H),
              c = 1000.f / (.1f - 1000.f), b = .1f * c;
  projection[0] = a;
  projection[5] = q;
  projection[10] = c;
  projection[11] = -1;
  projection[14] = b;
  inverse[0] = 1 / a;
  inverse[5] = 1 / q;
  inverse[11] = 1 / b;
  inverse[14] = -1;
  inverse[15] = c / b;
  for (unsigned n = 0; n < 4; ++n)
    identity[n * 5] = 1;
  ptr("DLSSG.CameraViewToClip", projection);
  ptr("DLSSG.ClipToCameraView", inverse);
  for (auto k :
       {"DLSSG.ClipToLensClip", "DLSSG.ClipToPrevClip", "DLSSG.PrevClipToClip"})
    ptr(k, identity);
  fp("DLSSG.MvecScaleX", 1.f / W);
  fp("DLSSG.MvecScaleY", 1.f / H);
  fp("DLSSG.CameraNear", .1f);
  fp("DLSSG.CameraFar", 1000.f);
  fp("DLSSG.CameraFOV", 1.0471975512f);
  fp("DLSSG.CameraAspectRatio", float(W) / H);
  for (auto k : {"DLSSG.JitterOffsetX", "DLSSG.JitterOffsetY",
                 "DLSSG.CameraPinholeOffsetX", "DLSSG.CameraPinholeOffsetY",
                 "DLSSG.CameraPosX", "DLSSG.CameraPosY", "DLSSG.CameraPosZ",
                 "DLSSG.CameraUpX", "DLSSG.CameraUpZ", "DLSSG.CameraRightY",
                 "DLSSG.CameraRightZ", "DLSSG.CameraFwdX", "DLSSG.CameraFwdY"})
    fp(k, 0.f);
  fp("DLSSG.CameraUpY", 1.f);
  fp("DLSSG.CameraRightX", 1.f);
  fp("DLSSG.CameraFwdZ", -1.f);
  for (auto k : {"DLSSG.ColorBuffersHDR", "DLSSG.DepthInverted",
                 "DLSSG.NotRenderingGameFrames", "DLSSG.OrthoProjection",
                 "DLSSG.MenuDetectionEnabled", "DLSSG.MvecDilated",
                 "DLSSG.MvecJittered", "DLSSG.AutomodeOverrideReset"})
    ui(k, 0);
  ui("DLSSG.CameraMotionIncluded", 1);
  ui("DLSSG.MultiFrameCount", 1);
  ui("DLSSG.MultiFrameIndex", 1);
  for (auto k :
       {"DLSSG.MVecsSubrectWidth", "DLSSG.DepthSubrectWidth",
        "DLSSG.InputBackbufferSubrectWidth",
        "DLSSG.OutputInterpolatedSubrectWidth", "DLSSG.BackbufferSubrectWidth"})
    ui(k, W);
  for (auto k : {"DLSSG.MVecsSubrectHeight", "DLSSG.DepthSubrectHeight",
                 "DLSSG.InputBackbufferSubrectHeight",
                 "DLSSG.OutputInterpolatedSubrectHeight",
                 "DLSSG.BackbufferSubrectHeight"})
    ui(k, H);
  for (auto k :
       {"DLSSG.MVecsSubrectBaseX", "DLSSG.MVecsSubrectBaseY",
        "DLSSG.DepthSubrectBaseX", "DLSSG.DepthSubrectBaseY",
        "DLSSG.InputBackbufferSubrectBaseX",
        "DLSSG.InputBackbufferSubrectBaseY",
        "DLSSG.OutputInterpolatedSubrectBaseX",
        "DLSSG.OutputInterpolatedSubrectBaseY", "DLSSG.BackbufferSubrectBaseX",
        "DLSSG.BackbufferSubrectBaseY"})
    ui(k, 0);
  fp("DLSSG.MvecInvalidValue", 3.402823466e38f);
  fp("DLSSG.MinRelativeLinearDepthObjectSeparation", 40.f);
  if(input_path){for(auto k:{"DLSSG.MVecsSubrectWidth","DLSSG.DepthSubrectWidth","DLSSG.InternalWidth"})ui(k,RW);
    for(auto k:{"DLSSG.MVecsSubrectHeight","DLSSG.DepthSubrectHeight","DLSSG.InternalHeight"})ui(k,RH);
    fp("DLSSG.MvecScaleX",1.f/RW);fp("DLSSG.MvecScaleY",1.f/RH);}
  unsigned valid = 0;
  for (unsigned n = 0; n < frames.size(); ++n) {
    const auto started=cpu_ms();
    auto &f = frames[n];
    api(d<PFN_vkResetCommandBuffer>("vkResetCommandBuffer")(command, 0),
        "reset Eval command");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    api(d<PFN_vkBeginCommandBuffer>("vkBeginCommandBuffer")(command, &begin),
        "begin Eval");
    g.upload(f.color, f.color_upload);
    g.upload(f.depth, f.depth_upload);
    g.upload(f.motion, f.motion_upload);
    g.barrier(f.output, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    VkClearColorValue magenta{{1, 0, 1, 1}};
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    d<PFN_vkCmdClearColorImage>("vkCmdClearColorImage")(
        command, f.output.image, VK_IMAGE_LAYOUT_GENERAL, &magenta, 1, &range);
    d<PFN_vkCmdFillBuffer>("vkCmdFillBuffer")(command, f.disabled.buffer, 0, 4,
                                              0xffffffffu);
    VkMemoryBarrier memory{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    memory.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    memory.dstAccessMask =
        VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    d<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(
        command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &memory, 0, nullptr, 0,
        nullptr);
    ptr("DLSSG.Backbuffer", &f.color.ngx);
    ptr("DLSSG.Depth", &f.depth.ngx);
    ptr("DLSSG.MVecs", &f.motion.ngx);
    ptr("DLSSG.OutputInterpolated", &f.output.ngx);
    ptr("DLSSG.OutputDisableInterpolation", &f.disabled.ngx);
    for (auto k : {"DLSSG.HUDLess", "DLSSG.UI", "DLSSG.UIAlpha",
                   "DLSSG.OutputReal", "DLSSG.BidirectionalDistortionField"})
      ptr(k, nullptr);
    std::array<std::array<float,16>,4> camera_matrices{};
    unsigned reset=n==0;
    if(input_path){const auto& m=game.at("frames").at(n);
      const char* source_keys[]={"camera_view_to_clip","clip_to_camera_view","clip_to_previous_clip","previous_clip_to_clip"};
      const char* target_keys[]={"DLSSG.CameraViewToClip","DLSSG.ClipToCameraView","DLSSG.ClipToPrevClip","DLSSG.PrevClipToClip"};
      for(unsigned k=0;k<4;++k){camera_matrices[k]=m.at(source_keys[k]).get<std::array<float,16>>();ptr(target_keys[k],camera_matrices[k].data());}
      const char* vector_keys[]={"camera_position","camera_up","camera_right","camera_forward"};
      const char* bases[]={"DLSSG.CameraPos","DLSSG.CameraUp","DLSSG.CameraRight","DLSSG.CameraFwd"};
      for(unsigned k=0;k<4;++k)for(unsigned j=0;j<3;++j){std::string key=std::string(bases[k])+"XYZ"[j];fp(key.c_str(),m.at(vector_keys[k]).at(j).get<float>());}
      fp("DLSSG.CameraNear",m.at("near").get<float>());fp("DLSSG.CameraFar",m.at("far").get<float>());fp("DLSSG.CameraFOV",m.at("vertical_fov").get<float>());
      fp("DLSSG.JitterOffsetX",m.at("jitter").at(0).get<float>());fp("DLSSG.JitterOffsetY",m.at("jitter").at(1).get<float>());
      reset=m.at("reset").get<bool>()||n==0;ull("DLSSG.BackbufferFrameID",m.at("source_game_frame").get<unsigned long long>());
    }else ull("DLSSG.BackbufferFrameID", n + 1);
    ui("DLSSG.Reset", reset);
    int r = sym<int (*)(void *, const void *, const void *)>(
        bridge, "fg_evaluate")(command, feature, params);
    printf("FG_EVALUATE frame=%u result=0x%08x reset=%u recorded_only=1\n", n,
           unsigned(r), reset);
    g.readback(f.output, f.readback);
    g.host(f.disabled, false);
    api(d<PFN_vkEndCommandBuffer>("vkEndCommandBuffer")(command), "end Eval");
    api(d<PFN_vkResetFences>("vkResetFences")(device, 1, &fence),
        "reset Eval fence");
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &command;
    inflight = true;
    g.safe = false;
    api(d<PFN_vkQueueSubmit>("vkQueueSubmit")(queue, 1, &si, fence),
        "submit Eval");
    api(d<PFN_vkWaitForFences>("vkWaitForFences")(device, 1, &fence, VK_TRUE,
                                                  10000000000ull),
        "Eval actual GPU fence");
    inflight = false;
    g.safe = true;
    api(d<PFN_vkDeviceWaitIdle>("vkDeviceWaitIdle")(device),
        "Eval device idle");
    printf("FG_EVAL_TIME frame=%u CPU_record_submit_fence_idle_ms=%.3f input_image_sets=%u\n",n,cpu_ms()-started,reuse_inputs?2u:6u);
    unsigned disabled = 0;
    memcpy(&disabled, f.disabled.mapped, 4);
    if (r == 1 && (disabled & 255) == 0)
      ++valid;
    printf("FG_EVAL_GPU_COMPLETE frame=%u result=0x%08x disable_word=0x%08x "
           "present_eligible=%u native_presents=0\n",
           n, unsigned(r), disabled, unsigned(r == 1 && (disabled & 255) == 0));
    if (n < 4 && r == 1) {
      auto dump = [&](const char *kind, const void *bytes) {
        char suffix[128];
        snprintf(suffix, sizeof suffix, "/fixture-%u-%s-1920x1080-rgba.raw", n,
                 kind);
        FILE *file = fopen((std::string(run) + suffix).c_str(), "wb");
        require(file, "fixture readback file");
        bool ok = fwrite(bytes, 1, pixels * 4, file) == pixels * 4;
        ok = fclose(file) == 0 && ok;
        require(ok, "fixture readback write");
      };
      dump("real", f.color_upload.mapped);
      dump("interp", f.readback.mapped);
    }
    // 310.2.1 logs "Re-initializing ... recoverable error state" on its initial
    // zero-size cache. Keep that actual failure, drain it, then try the next
    // real fixture frame. This bounded warm-up never makes failed output
    // eligible.
    if (r != 1 && unsigned(r) != 0xbad00005u)
      break;
  }
  printf("FG_FIXTURE_RESULT eligible_outputs=%u scope=%s\n",valid,
         input_path?"captured_Bloodborne_offscreen_not_Present":"synthetic_offscreen_not_Bloodborne_or_Present");
}
