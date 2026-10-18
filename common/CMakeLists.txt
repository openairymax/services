# ===========================================================================
# services/common — daemon 框架（主循环 / fd 事件源 / IPC 注册 / 心跳）
# 决策 I1：同一子仓内的共享框架库允许存在，不跨子仓共享。
# ===========================================================================

add_library(airy_daemon STATIC
	daemon/dmn_beat.c
	daemon/dmn_core.c
	daemon/dmn_crc.c
	daemon/dmn_event.c
	daemon/dmn_ipc.c
	daemon/dmn_loop.c
	daemon/dmn_sig.c
)

target_compile_features(airy_daemon PUBLIC c_std_11)

find_package(ZLIB REQUIRED)

target_link_libraries(airy_daemon PUBLIC ZLIB::ZLIB)

target_include_directories(airy_daemon
	PUBLIC
		${CMAKE_CURRENT_SOURCE_DIR}/include
)

# glibc 扩展可见性（signalfd / clock_gettime / SOCK_SEQPACKET）由
# 各实现文件在包含系统头之前自行定义 _GNU_SOURCE，避免构建系统与
# 源文件重复定义触发 -Werror。

# [SC] 头文件物理宿主置于系统头之后，避免遮蔽 glibc 的 <linux/*>。
target_compile_options(airy_daemon
	PUBLIC
		-idirafter ${CMAKE_SOURCE_DIR}/../kernel/include/uapi
)
