project(libs)

include(../CMakeConfig.txt)

if(${MODULESPACK} MATCHES "OFF" OR WIN32)
	set(${PROJECT_NAME}_files "" CACHE INTERNAL "")

	add_subdirectory(polarssl)
	set(${PROJECT_NAME}_files ${${PROJECT_NAME}_files} ${polarssl_files} CACHE INTERNAL "")

	if(NOT WIN32)
		add_subdirectory(wiringx)
		set(${PROJECT_NAME}_files ${${PROJECT_NAME}_files} ${wiringx_files} CACHE INTERNAL "")
	endif()

	add_subdirectory(avrdude)
	set(${PROJECT_NAME}_files ${${PROJECT_NAME}_files} ${avrdude_files} CACHE INTERNAL "")

	if(${ZWAVE} MATCHES "ON")
		add_subdirectory(openzwave)	
		set(${PROJECT_NAME}_files ${${PROJECT_NAME}_files} ${openzwave_files} CACHE INTERNAL "")
	endif()

	add_subdirectory(pilight)
	set(${PROJECT_NAME}_files ${${PROJECT_NAME}_files} ${pilight_files} CACHE INTERNAL "")
else()
	add_subdirectory(pilight)
endif()	
