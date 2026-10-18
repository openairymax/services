# ===========================================================================
# services/common — daemon 框架（主循环 / fd 事件源 / IPC 注册 / 心跳）
# 决策 I1：同一子仓内的共享框架库允许存在，不跨子仓共享。
# TK-01-01 仅交付契约头文件，故为 INTERFACE 目标（无空静态库）。
# ===========================================================================

add_library(airy_daemon INTERFACE)

target_compile_features(airy_daemon INTERFACE c_std_11)

target_include_directories(airy_daemon INTERFACE
	${CMAKE_CURRENT_SOURCE_DIR}/include
)

# [SC] 头文件物理宿主置于系统头之后，避免遮蔽 glibc 的 <linux/*>。
target_compile_options(airy_daemon INTERFACE
	-idirafter ${CMAKE_SOURCE_DIR}/../kernel/include/uapi
)
