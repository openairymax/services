# ===========================================================================
# macro_d daemon CMakeLists.txt
# Micro-Supervisor 的故障裁决守护：配置校验 / 通道排空 / 裁决记账
# ===========================================================================

add_library(airy_macro STATIC
	macro_cfg.c
)

target_include_directories(airy_macro
	PUBLIC
		${CMAKE_CURRENT_SOURCE_DIR}
)

# [SC] 监督通道与错误码物理宿主：kernel/include/uapi/linux/airymax/
target_compile_options(airy_macro
	PUBLIC
		-idirafter ${CMAKE_SOURCE_DIR}/../kernel/include/uapi
)

add_executable(macro_d
	macro_d.c
)

target_link_libraries(macro_d
	PRIVATE
		airy_macro
		airy_daemon
)

install(TARGETS macro_d
	RUNTIME DESTINATION sbin
)

# 默认配置：0640 root:airy，与 A-UCS 配置根一致
install(FILES etc/macro-superv.conf
	DESTINATION etc/agentrt
	PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ
)

if(BUILD_TESTING)
	add_subdirectory(tests)
endif()
