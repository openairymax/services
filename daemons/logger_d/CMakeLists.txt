# ===========================================================================
# logger_d daemon CMakeLists.txt
# A-ULP 内核日志环的消费守护：配置校验 / 环读取 / 落盘轮转
# ===========================================================================

add_library(airy_logd STATIC
	logd_cfg.c
	logd_ring.c
	logd_file.c
)

target_include_directories(airy_logd
	PUBLIC
		${CMAKE_CURRENT_SOURCE_DIR}
)

# [SC] 日志环与错误码物理宿主：kernel/include/uapi/linux/airymax/
target_compile_options(airy_logd
	PUBLIC
		-idirafter ${CMAKE_SOURCE_DIR}/../kernel/include/uapi
)

add_executable(logger_d
	logger_d.c
)

target_link_libraries(logger_d
	PRIVATE
		airy_logd
		airy_daemon
)

install(TARGETS logger_d
	RUNTIME DESTINATION sbin
)

# 默认配置：0640 root:airy，与 A-UCS 配置根一致
install(FILES etc/logger.conf
	DESTINATION etc/agentrt
	PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ
)

if(BUILD_TESTING)
	add_subdirectory(tests)
endif()
