# ===========================================================================
# config_d daemon CMakeLists.txt
# A-UCS 统一配置守护：INI 装载 / schema 校验 / 快照热重载
# ===========================================================================

add_library(airy_cfg STATIC
	cfg_core.c
	cfg_ini.c
	cfg_sys.c
)

target_include_directories(airy_cfg
	PUBLIC
		${CMAKE_CURRENT_SOURCE_DIR}
)

# [SC] 错误码物理宿主：kernel/include/uapi/linux/airymax/error.h
target_compile_options(airy_cfg
	PUBLIC
		-idirafter ${CMAKE_SOURCE_DIR}/../kernel/include/uapi
)

find_package(Threads REQUIRED)

target_link_libraries(airy_cfg
	PUBLIC
		Threads::Threads
)

add_executable(config_d
	config_d.c
)

target_link_libraries(config_d
	PRIVATE
		airy_cfg
		airy_daemon
)

install(TARGETS config_d
	RUNTIME DESTINATION sbin
)

# 默认配置：0640 root:agentrt，与 A-UCS 设计一致
install(FILES etc/agentrt.conf
	DESTINATION etc/agentrt
	PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ
)

if(BUILD_TESTING)
	add_subdirectory(tests)
endif()
