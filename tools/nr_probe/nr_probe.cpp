// SPDX-License-Identifier: MIT
#include "nr_frame_desc.h"
#include "vk_test_context.h"
#include <cerrno>
#include <cmath>
#include <fcntl.h>
#include <memory>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
using namespace nr;
static pid_t child_pid = -1;
struct Packet {
  NRFrameDesc desc{};
  std::vector<int> fds;
  ~Packet() {
    for (int fd : fds)
      if (fd >= 0)
        close(fd);
  }
};
void readable(int socket) {
  pollfd p{socket, POLLIN, 0};
  int n;
  do {
    n = poll(&p, 1, 5000);
  } while (n < 0 && errno == EINTR);
  require(n > 0, "E_IPC_TIMEOUT");
  require(p.revents & POLLIN, "E_PEER_CLOSED");
}
void send_frame(int socket, const NRFrameDesc &d,
                const std::vector<int> &fds = {}, bool truncated = false) {
  iovec io{const_cast<NRFrameDesc *>(&d),
           truncated ? sizeof(d) - 1 : sizeof(d)};
  alignas(cmsghdr) char control[CMSG_SPACE(FdCount * sizeof(int))]{};
  msghdr msg{};
  msg.msg_iov = &io;
  msg.msg_iovlen = 1;
  if (!fds.empty()) {
    msg.msg_control = control;
    msg.msg_controllen = CMSG_SPACE(fds.size() * sizeof(int));
    auto c = CMSG_FIRSTHDR(&msg);
    c->cmsg_level = SOL_SOCKET;
    c->cmsg_type = SCM_RIGHTS;
    c->cmsg_len = CMSG_LEN(fds.size() * sizeof(int));
    std::memcpy(CMSG_DATA(c), fds.data(), fds.size() * sizeof(int));
  }
  require(sendmsg(socket, &msg, MSG_NOSIGNAL) == ssize_t(io.iov_len), "E_SEND");
}
void receive_frame(int socket, Packet &p, bool first) {
  readable(socket);
  iovec io{&p.desc, sizeof(p.desc)};
  alignas(cmsghdr) char control[CMSG_SPACE(FdCount * sizeof(int))]{};
  msghdr m{};
  m.msg_iov = &io;
  m.msg_iovlen = 1;
  m.msg_control = control;
  m.msg_controllen = sizeof(control);
  auto n = recvmsg(socket, &m, MSG_CMSG_CLOEXEC);
  for (auto c = CMSG_FIRSTHDR(&m); c; c = CMSG_NXTHDR(&m, c)) {
    require(c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS,
            "E_CONTROL");
    auto count = (c->cmsg_len - CMSG_LEN(0)) / sizeof(int);
    auto f = reinterpret_cast<int *>(CMSG_DATA(c));
    p.fds.insert(p.fds.end(), f, f + count);
  }
  require(n > 0, "E_PEER_CLOSED");
  require(!(m.msg_flags & (MSG_TRUNC | MSG_CTRUNC)), "E_PACKET_TRUNCATED");
  require(first || p.fds.empty(), "E_UNEXPECTED_FDS");
  validate(p.desc, size_t(n), first ? p.fds.size() : FdCount);
}
std::vector<const char *> extensions() {
  return {VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
          VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME};
}
void image_support(VkTestContext &v, VkFormat format, uint32_t w, uint32_t h) {
  VkPhysicalDeviceExternalImageFormatInfo ex{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO};
  ex.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
  VkPhysicalDeviceImageFormatInfo2 q{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2};
  q.pNext = &ex;
  q.format = format;
  q.type = VK_IMAGE_TYPE_2D;
  q.tiling = VK_IMAGE_TILING_OPTIMAL;
  q.usage = Usage;
  VkExternalImageFormatProperties ep{
      VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES};
  VkImageFormatProperties2 p{VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2};
  p.pNext = &ep;
  VKCHECK(vkGetPhysicalDeviceImageFormatProperties2(v.physical, &q, &p));
  auto bits = ep.externalMemoryProperties.externalMemoryFeatures;
  require((bits & (VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT |
                   VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT)) ==
              (VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT |
               VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT),
          "E_IMAGE_EXTERNAL_UNSUPPORTED");
  require(ep.externalMemoryProperties.compatibleHandleTypes &
              VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT,
          "E_IMAGE_HANDLE_UNSUPPORTED");
  require(w <= p.imageFormatProperties.maxExtent.width &&
              h <= p.imageFormatProperties.maxExtent.height,
          "E_DEVICE_EXTENT");
}
TestImage make_image(VkTestContext &v, ImageDesc &d, int *import_fd = nullptr) {
  image_support(v, VkFormat(d.format), d.width, d.height);
  VkExternalMemoryImageCreateInfo ex{
      VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
  ex.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
  VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ci.pNext = &ex;
  ci.imageType = VK_IMAGE_TYPE_2D;
  ci.format = VkFormat(d.format);
  ci.extent = {d.width, d.height, 1};
  ci.mipLevels = ci.arrayLayers = 1;
  ci.samples = VK_SAMPLE_COUNT_1_BIT;
  ci.tiling = VK_IMAGE_TILING_OPTIMAL;
  ci.usage = Usage;
  TestImage image;
  VKCHECK(vkCreateImage(v.device, &ci, nullptr, &image.image));
  VkMemoryDedicatedRequirements dedicated{
      VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS};
  VkMemoryRequirements2 req{VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};
  req.pNext = &dedicated;
  VkImageMemoryRequirementsInfo2 qi{
      VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2};
  qi.image = image.image;
  vkGetImageMemoryRequirements2(v.device, &qi, &req);
  VkMemoryDedicatedAllocateInfo di{
      VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
  di.image = image.image;
  VkExportMemoryAllocateInfo export_info{
      VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO};
  export_info.pNext = &di;
  export_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
  VkImportMemoryFdInfoKHR ii{VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR};
  ii.pNext = &di;
  ii.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
  if (import_fd) {
    require(d.allocation_size == req.memoryRequirements.size &&
                (req.memoryRequirements.memoryTypeBits & (1u << d.memory_type)),
            "E_IMPORT_ALLOCATION");
    ii.fd = *import_fd;
  } else {
    d.allocation_size = req.memoryRequirements.size;
    d.memory_type = v.memory_type(req.memoryRequirements.memoryTypeBits,
                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  }
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  ai.pNext =
      import_fd ? static_cast<void *>(&ii) : static_cast<void *>(&export_info);
  ai.allocationSize = d.allocation_size;
  ai.memoryTypeIndex = d.memory_type;
  VKCHECK(vkAllocateMemory(v.device, &ai, nullptr, &image.memory));
  if (import_fd)
    *import_fd = -1; // successful Vulkan import consumes this fd
  VKCHECK(vkBindImageMemory(v.device, image.image, image.memory, 0));
  return image;
}
int export_image(VkTestContext &v, const TestImage &i) {
  VkMemoryGetFdInfoKHR q{VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR};
  q.memory = i.memory;
  q.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
  int fd = -1;
  VKCHECK(v.proc<PFN_vkGetMemoryFdKHR>("vkGetMemoryFdKHR")(v.device, &q, &fd));
  return fd;
}
VkSemaphore make_semaphore(VkTestContext &v, int *fd = nullptr) {
  VkPhysicalDeviceExternalSemaphoreInfo qi{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO};
  qi.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
  VkExternalSemaphoreProperties p{
      VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES};
  vkGetPhysicalDeviceExternalSemaphoreProperties(v.physical, &qi, &p);
  require((p.externalSemaphoreFeatures & 3) == 3,
          "E_SEMAPHORE_EXTERNAL_UNSUPPORTED");
  VkExportSemaphoreCreateInfo ex{
      VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO};
  ex.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
  VkSemaphoreCreateInfo ci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  ci.pNext = fd ? nullptr : &ex;
  VkSemaphore s{};
  VKCHECK(vkCreateSemaphore(v.device, &ci, nullptr, &s));
  if (fd) {
    VkImportSemaphoreFdInfoKHR ii{
        VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR};
    ii.semaphore = s;
    ii.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
    ii.fd = *fd;
    VKCHECK(v.proc<PFN_vkImportSemaphoreFdKHR>("vkImportSemaphoreFdKHR")(
        v.device, &ii));
    *fd = -1;
  }
  return s;
}
int export_semaphore(VkTestContext &v, VkSemaphore s) {
  VkSemaphoreGetFdInfoKHR q{VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR};
  q.semaphore = s;
  q.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
  int fd = -1;
  VKCHECK(v.proc<PFN_vkGetSemaphoreFdKHR>("vkGetSemaphoreFdKHR")(v.device, &q,
                                                                 &fd));
  return fd;
}
std::array<float, 4> pattern(uint32_t role, uint64_t sequence,
                             size_t pixel = 0) {
  if (role == Depth)
    return {.25f + float(pixel % 17) / 64, 0, 0, 0};
  if (role == Motion)
    return {float(sequence), -float(sequence), 0, 0};
  if (role == Output)
    return {-1, -1, -1, -1};
  return {float(sequence) / 16.f, float(pixel % 127) / 128.f,
          float((pixel / 127) % 127) / 128.f, 1.f};
}
void verify(const float *data, uint32_t role, uint64_t seq, size_t pixels) {
  for (size_t i = 0; i < pixels; ++i)
    for (uint32_t c = 0; c < Channels[role]; ++c)
      if (data[i * Channels[role] + c] != pattern(role, seq, i)[c])
        throw std::runtime_error("E_READBACK role=" + std::to_string(role) +
                                 " pixel=" + std::to_string(i) +
                                 " channel=" + std::to_string(c));
}
void cleanup(VkTestContext &v, TestImage *images, VkSemaphore ready,
             VkSemaphore done, Readback &rb) {
  rb.destroy(v);
  for (uint32_t i = 0; i < ImageCount; ++i) {
    vkDestroyImage(v.device, images[i].image, nullptr);
    vkFreeMemory(v.device, images[i].memory, nullptr);
  }
  vkDestroySemaphore(v.device, ready, nullptr);
  vkDestroySemaphore(v.device, done, nullptr);
  v.destroy();
}
int worker(int socket) {
  Packet first;
  receive_frame(socket, first, true);
  auto d = first.desc;
  auto initial = d;
  VkTestContext v;
  v.create_instance();
  v.select(d.device_uuid);
  require(!std::memcmp(v.ids.driverUUID, d.driver_uuid, VK_UUID_SIZE),
          "E_DRIVER_UUID");
  v.create_device(extensions());
  TestImage images[ImageCount];
  for (uint32_t i = 0; i < ImageCount; ++i)
    images[i] = make_image(v, d.images[i], &first.fds[i]);
  auto ready = make_semaphore(v, &first.fds[4]),
       done = make_semaphore(v, &first.fds[5]);
  const size_t pixels = size_t(d.render_width) * d.render_height;
  Readback rb;
  rb.create(v, pixels * 11 * sizeof(float) + 64);
  for (;;) {
    v.begin();
    for (auto &im : images)
      image_barrier(v, im.image, VK_IMAGE_LAYOUT_GENERAL,
                    VK_IMAGE_LAYOUT_GENERAL, VK_QUEUE_FAMILY_EXTERNAL,
                    v.family);
    if (d.mode == PassThrough) {
      VkImageCopy c{};
      c.srcSubresource =
          c.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
      c.extent = {d.render_width, d.render_height, 1};
      vkCmdCopyImage(v.cmd, images[Color].image, VK_IMAGE_LAYOUT_GENERAL,
                     images[Output].image, VK_IMAGE_LAYOUT_GENERAL, 1, &c);
      image_barrier(v, images[Output].image, VK_IMAGE_LAYOUT_GENERAL,
                    VK_IMAGE_LAYOUT_GENERAL);
    }
    size_t offset = 0;
    for (uint32_t i = 0; i < ImageCount; ++i) {
      rb.copy(v, images[i].image, d.render_width, d.render_height, offset);
      offset =
          (offset + pixels * Channels[i] * sizeof(float) + 15) & ~size_t(15);
    }
    rb.host_barrier(v);
    for (auto &im : images)
      image_barrier(v, im.image, VK_IMAGE_LAYOUT_GENERAL,
                    VK_IMAGE_LAYOUT_GENERAL, v.family,
                    VK_QUEUE_FAMILY_EXTERNAL);
    v.submit(ready, done);
    v.wait();
    offset = 0;
    for (uint32_t i = 0; i < ImageCount; ++i) {
      verify(reinterpret_cast<float *>(static_cast<char *>(rb.mapped) + offset),
             i == Output && d.mode == PassThrough ? Color : i, d.sequence,
             pixels);
      offset =
          (offset + pixels * Channels[i] * sizeof(float) + 15) & ~size_t(15);
    }
    std::printf(
        "NRPROBE worker sequence=%llu guides=OK output=OK reset=%u mode=%u\n",
        (unsigned long long)d.sequence, d.reset, d.mode);
    require(send(socket, &d.sequence, sizeof(d.sequence), MSG_NOSIGNAL) ==
                sizeof(d.sequence),
            "E_ACK_SEND");
    readable(socket);
    char ch;
    auto n = recv(socket, &ch, 1, MSG_PEEK);
    if (n == 1 && ch == 'Q') {
      recv(socket, &ch, 1, 0);
      break;
    }
    Packet next;
    receive_frame(socket, next, false);
    auto expected = initial;
    expected.sequence = d.sequence + 1;
    expected.reset = next.desc.reset;
    expected.jitter_x = next.desc.jitter_x;
    expected.jitter_y = next.desc.jitter_y;
    expected.frame_ms = next.desc.frame_ms;
    require(std::memcmp(&expected, &next.desc, sizeof(expected)) == 0,
            "E_SEQUENCE_OR_RESOURCE_CHANGE");
    d = next.desc;
  }
  cleanup(v, images, ready, done, rb);
  close(socket);
  return 0;
}
int producer(const char *exe, uint32_t mode, uint32_t frames, uint32_t width,
             uint32_t height, const std::string &fault) {
  require(frames >= 1 && frames <= 10000 && width && height && width <= 4096 &&
              height <= 4096,
          "E_ARGUMENT");
  NRFrameDesc d;
  d.mode = mode;
  d.render_width = d.output_width = width;
  d.render_height = d.output_height = height;
  d.session = uint64_t(getpid());
  VkTestContext v;
  v.create_instance();
  v.select();
  v.create_device(extensions());
  std::memcpy(d.device_uuid, v.ids.deviceUUID, VK_UUID_SIZE);
  std::memcpy(d.driver_uuid, v.ids.driverUUID, VK_UUID_SIZE);
  TestImage images[ImageCount];
  Packet exports;
  for (uint32_t i = 0; i < ImageCount; ++i) {
    d.images[i] = {i,
                   uint32_t(Formats[i]),
                   width,
                   height,
                   Usage,
                   VK_IMAGE_TILING_OPTIMAL,
                   VK_IMAGE_LAYOUT_GENERAL,
                   VK_IMAGE_ASPECT_COLOR_BIT,
                   0,
                   i,
                   1,
                   1,
                   0,
                   0};
    images[i] = make_image(v, d.images[i]);
    exports.fds.push_back(export_image(v, images[i]));
  }
  auto ready = make_semaphore(v), done = make_semaphore(v);
  exports.fds.push_back(export_semaphore(v, ready));
  exports.fds.push_back(export_semaphore(v, done));
  validate(d, sizeof(d), exports.fds.size());
  for (int fd : exports.fds)
    require(fcntl(fd, F_SETFD, FD_CLOEXEC) == 0, "E_FD_CLOEXEC");
  int sockets[2];
  require(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sockets) == 0,
          "E_SOCKETPAIR");
  std::string fdarg = std::to_string(sockets[1]);
  child_pid = fork();
  require(child_pid >= 0, "E_FORK");
  if (child_pid == 0) {
    close(sockets[0]);
    fcntl(sockets[1], F_SETFD, 0);
    execl(exe, exe, "--worker", fdarg.c_str(), nullptr);
    _exit(127);
  }
  close(sockets[1]);
  Readback rb;
  rb.create(v, size_t(width) * height * 11 * sizeof(float) + 64);
  for (uint32_t frame = 1; frame <= frames; ++frame) {
    d.sequence = frame;
    d.reset = frame == 1 || frame == 3;
    d.jitter_x = frame % 2 ? .25f : -.25f;
    d.jitter_y = frame % 2 ? -.125f : .125f;
    const size_t pixels = size_t(width) * height;
    size_t offset = 0;
    for (uint32_t role = 0; role < ImageCount; ++role) {
      auto data =
          reinterpret_cast<float *>(static_cast<char *>(rb.mapped) + offset);
      for (size_t pixel = 0; pixel < pixels; ++pixel)
        for (uint32_t c = 0; c < Channels[role]; ++c)
          data[pixel * Channels[role] + c] = pattern(role, frame, pixel)[c];
      offset =
          (offset + pixels * Channels[role] * sizeof(float) + 15) & ~size_t(15);
    }
    if (fault == "corrupt")
      static_cast<float *>(rb.mapped)[0] = 99;
    v.begin();
    offset = 0;
    VkMemoryBarrier upload{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    upload.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    upload.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(v.cmd, VK_PIPELINE_STAGE_HOST_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &upload, 0,
                         nullptr, 0, nullptr);
    for (uint32_t i = 0; i < ImageCount; ++i) {
      image_barrier(v, images[i].image,
                    frame == 1 ? VK_IMAGE_LAYOUT_UNDEFINED
                               : VK_IMAGE_LAYOUT_GENERAL,
                    VK_IMAGE_LAYOUT_GENERAL);
      VkBufferImageCopy c{};
      c.bufferOffset = offset;
      c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
      c.imageExtent = {width, height, 1};
      vkCmdCopyBufferToImage(v.cmd, rb.buffer, images[i].image,
                             VK_IMAGE_LAYOUT_GENERAL, 1, &c);
      offset =
          (offset + pixels * Channels[i] * sizeof(float) + 15) & ~size_t(15);
      image_barrier(v, images[i].image, VK_IMAGE_LAYOUT_GENERAL,
                    VK_IMAGE_LAYOUT_GENERAL, v.family,
                    VK_QUEUE_FAMILY_EXTERNAL);
    }
    v.submit({}, fault == "timeout" ? VK_NULL_HANDLE : ready);
    v.wait();
    auto wire = d;
    if (fault == "version")
      wire.version = 99;
    if (fault == "uuid")
      wire.device_uuid[0] ^= 255;
    if (fault == "format")
      wire.images[0].format = VK_FORMAT_UNDEFINED;
    if (fault == "extent")
      wire.render_width = 0;
    if (fault == "sequence" && frame == 2)
      wire.sequence = 1;
    auto fds = frame == 1 ? exports.fds : std::vector<int>{};
    if (fault == "drop-fd" && !fds.empty())
      fds.pop_back();
    if (fault == "peer-exit") {
      kill(child_pid, SIGKILL);
      throw std::runtime_error("E_PEER_INJECTED");
    }
    send_frame(sockets[0], wire, fds, fault == "truncated");
    // ACK is after worker's GPU fence, but GPU visibility/ownership is
    // established by the imported binary semaphore and acquire barriers below,
    // not by the socket message.
    readable(sockets[0]);
    uint64_t ack = 0;
    require(recv(sockets[0], &ack, sizeof(ack), 0) == sizeof(ack),
            "E_PEER_CLOSED");
    require(ack == d.sequence, "E_ACK_SEQUENCE");
    v.begin();
    for (auto &im : images)
      image_barrier(v, im.image, VK_IMAGE_LAYOUT_GENERAL,
                    VK_IMAGE_LAYOUT_GENERAL, VK_QUEUE_FAMILY_EXTERNAL,
                    v.family);
    rb.copy(v, images[Output].image, width, height);
    rb.host_barrier(v);
    v.submit(done);
    v.wait();
    verify(static_cast<float *>(rb.mapped),
           mode == PassThrough ? Color : Output, frame, size_t(width) * height);
    std::printf("NRPROBE parent sequence=%u output=OK size=%ux%u mode=%s\n",
                frame, width, height,
                mode == PassThrough ? "pass-through" : "no-op");
  }
  require(send(sockets[0], "Q", 1, MSG_NOSIGNAL) == 1, "E_SHUTDOWN_SEND");
  close(sockets[0]);
  int status = 0;
  require(waitpid(child_pid, &status, 0) == child_pid && WIFEXITED(status) &&
              WEXITSTATUS(status) == 0,
          "E_WORKER_EXIT");
  child_pid = -1;
  cleanup(v, images, ready, done, rb);
  std::puts("NRPROBE PASS transport=linux-vulkan-opaque-fd images=4 "
            "semaphores=2 NR=NOT_IMPLEMENTED");
  return 0;
}
int main(int argc, char **argv) {
  setvbuf(stdout, nullptr, _IOLBF, 0);
  signal(SIGPIPE, SIG_IGN);
  try {
    if (argc == 3 && std::string(argv[1]) == "--worker")
      return worker(std::stoi(argv[2]));
    uint32_t mode = PassThrough, frames = 4, width = 64, height = 48;
    std::string fault;
    for (int i = 1; i < argc; ++i) {
      std::string a = argv[i];
      require(i + 1 < argc, "E_ARGUMENT");
      std::string value = argv[++i];
      if (a == "--mode") {
        require(value == "no-op" || value == "pass-through", "E_MODE");
        mode = value == "no-op" ? NoOp : PassThrough;
      } else if (a == "--frames")
        frames = std::stoul(value);
      else if (a == "--width")
        width = std::stoul(value);
      else if (a == "--height")
        height = std::stoul(value);
      else if (a == "--fault") {
        fault = value;
        require(fault == "version" || fault == "uuid" || fault == "format" ||
                    fault == "drop-fd" || fault == "truncated" ||
                    fault == "peer-exit" || fault == "timeout" ||
                    fault == "corrupt" || fault == "sequence" ||
                    fault == "extent",
                "E_FAULT");
      } else
        throw std::runtime_error("E_ARGUMENT");
    }
    return producer("/proc/self/exe", mode, frames, width, height, fault);
  } catch (const std::exception &e) {
    std::fprintf(stderr, "NRPROBE FAIL pid=%d %s NR=NOT_IMPLEMENTED\n",
                 int(getpid()), e.what());
    if (child_pid > 0) {
      kill(child_pid, SIGKILL);
      waitpid(child_pid, nullptr, 0);
    }
    // A failed or deliberately unsignalled GPU submission must not enter an
    // unbounded vkDeviceWaitIdle/destroy path. The OS reclaims this disposable
    // test process.
    std::fflush(nullptr);
    _exit(2);
  }
}
