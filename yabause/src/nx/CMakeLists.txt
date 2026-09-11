project(yabause-nx)

yab_port_start()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

#add_subdirectory(pugixml)
#add_subdirectory(nanogui-sdl)

#------------------------------------------------------------------------------
# nlohmann/json
#  * sets EXTERNAL_JSON_INCLUDE_DIR 

set(PORT_INCLUDE_DIRS "../" )
set(PORT_LIBRARIES EGL GLESv2 glapi drm_nouveau nx m SDL2  )

#message("Nlohmann/json dir =" ${EXTERNAL_JSON_INCLUDE_DIR})

set(yabause_nx_SOURCES
	main.cpp
	../gbastation/YabLog.c
	../gbastation/Browser.cpp
	../gbastation/NvgUi.cpp
	../gbastation/UiFont.cpp
	../gbastation/Settings.cpp
	../thr-nx.c
	#InputManager.h
	#InputManager.cpp
	#InputConfig.h
	#InputConfig.cpp
	#MenuScreen.h
	#MenuScreen.cpp
)
include_directories( ../gbastation )


include_directories( ${YABAUSE_INCLUDE_DIR} ${PORT_INCLUDE_DIRS} ${Boost_INCLUDE_DIR} "nanogui-sdl/include" "nanogui-sdl/ext/eigen" )
link_directories( /opt/devkitpro/devkitA64/lib/ /opt/devkitpro/libnx/lib/ /opt/devkitpro/portlibs/switch/lib/  ) 
add_executable(yabause-nx ${yabause_nx_SOURCES})
add_dependencies(yabause-nx libchdr)
target_link_libraries(yabause-nx yabause ${LIBCHDR_LIBRARIES} ${YABAUSE_LIBRARIES} ${PORT_LIBRARIES} stdc++fs z)

set( CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -specs=/opt/devkitpro/libnx/switch.specs -g -Wl,-Map,memory.map" )

#add_dependencies(yabause-nx Json png zlib)
yab_port_success(yabause-nx)

#install(TARGETS yabause-xu4 DESTINATION "/opt/retropie/emulators/yabasanshiro/")
#install(FILES "README.md" DESTINATION "/opt/retropie/emulators/yabasanshiro/" )

#------------------------------------------------------------------------------
# romfs: embed the Material icon font so the boot page can draw folder/file/
# back glyphs on Switch (the system only ships text + NintendoExt).
set(GBA_ROMFS_DIR "${CMAKE_BINARY_DIR}/romfs")
file(MAKE_DIRECTORY "${GBA_ROMFS_DIR}/font")
set(GBA_MATERIAL_FONT "${CMAKE_CURRENT_SOURCE_DIR}/../../../res/font/MaterialIcons-Regular.ttf")
if (EXISTS "${GBA_MATERIAL_FONT}")
	file(COPY "${GBA_MATERIAL_FONT}" DESTINATION "${GBA_ROMFS_DIR}/font")
endif()

add_custom_command(TARGET yabause-nx
        POST_BUILD
        COMMAND /bin/bash ${CMAKE_CURRENT_SOURCE_DIR}/genSwitchExecutables.sh
                "${GBA_ROMFS_DIR}"
				)
				
install(TARGETS yabause-nx DESTINATION "bin")
install(FILES "README.md" DESTINATION "." )
