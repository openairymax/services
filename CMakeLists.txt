# ===========================================================================
# agent-linux (AirymaxOS) services submodule CMakeLists.txt
# 构建入口：12 daemon + 用户态子系统
# 决策 F1：最小可编译骨架（非桩）
# ===========================================================================

cmake_minimum_required(VERSION 3.20)

project(airy_services
	VERSION 0.1.1
	LANGUAGES C
	DESCRIPTION "AirymaxOS services submodule — 12 daemons + userland")

# ---------------------------------------------------------------------------
# C 标准：C11（用户态 daemon）
# ---------------------------------------------------------------------------
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# [SC] 头文件引用（OS-IRON-014 / 决策 G1）
# 唯一物理宿主：kernel/include/uapi/linux/airymax/
# 禁止物理副本，通过 -idirafter 引用：该树是内核 UAPI 副本，
# 置于系统头之后可避免遮蔽 glibc 的 <linux/*>，同时仍解析
# <linux/airymax/*>。
# ---------------------------------------------------------------------------
add_compile_options(-idirafter ${CMAKE_SOURCE_DIR}/../kernel/include/uapi)

# ---------------------------------------------------------------------------
# 编译选项（OS-STD-FMT-001/002）
# ---------------------------------------------------------------------------
add_compile_options(
	-Wall
	-Wextra
	-Werror
	-Wno-unused-parameter
	-fstack-protector-strong
	-D_FORTIFY_SOURCE=2
)

# ---------------------------------------------------------------------------
# 子目录：daemon 框架 + 12 daemon
# 0.1.1 仅 macro_d 可编译，其余 1.0.1 补齐
# ---------------------------------------------------------------------------
add_subdirectory(common)
add_subdirectory(daemons/macro_d)

# ---------------------------------------------------------------------------
# 安装规则
# ---------------------------------------------------------------------------
install(TARGETS macro_d
	RUNTIME DESTINATION bin
)
