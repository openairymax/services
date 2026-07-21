# ===========================================================================
# macro_d daemon CMakeLists.txt
# Macro-Supervisor 用户态裁决主循环（[IND]）
# 决策 F1：真实可编译实现（非桩）
# ===========================================================================

# 可执行目标 macro_d
add_executable(macro_d
	macro_d.c
)

# [SC] 头文件引用已由顶层 CMakeLists.txt 通过 include_directories 配置

# 链接库
target_link_libraries(macro_d
	PRIVATE
		rt
)

# 编译定义
target_compile_definitions(macro_d
	PRIVATE
		_GNU_SOURCE
)

# 安装
install(TARGETS macro_d
	RUNTIME DESTINATION bin
)
