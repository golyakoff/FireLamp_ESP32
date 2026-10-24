set(EMBED ../files)

idf_component_register(
	INCLUDE_DIRS .
	SRCS
		common.c
		main.c
		settings.c
		storage.c
		i2c_bus.c
		eeprom.c
		ds3231.c
		systime.c
		schedule.c
		scheduler.c
		demo_core.c
		demo.c
		ota.c
		power.c
		ble_server.c
		updater.c
		wifi.c
		bus.c
		effect.c
		code_hash.c
		surface.c
		input.c
		webserver.c
		api.c
		palettes.c
		utils.c
		
		effects/crazybees.c
		effects/dna.c
		effects/fire.c
		effects/gyver_fire.c
		effects/petrick_fire.c
		effects/lamp.c
		effects/lava.c
		effects/matrix.c
		effects/plasma_waves.c
		effects/rain.c
		effects/rays.c
		effects/sparkles.c
		effects/waterfall.c
		effects/metaballs.c
		effects/water.c
		effects/holiday.c
		effects/hoop.c
		effects/twister.c
		effects/flower.c
		effects/polar_lights.c
		effects/gif.c
		effects/wheel.c
		effects/peacock.c
		effects/diagonals.c
		effects/whirls.c
		effects/sun.c
		effects/spaceships.c
		effects/fireflies.c
		effects/tunnel.c
		effects/dunes.c
		effects/octopus.c
		effects/nuclear.c
		effects/starfall.c
		effects/storm.c
		effects/twinkles.c
		effects/butterflies.c
		effects/streams.c
		effects/trail_fireflies.c
		effects/glass_drops.c
		effects/magma.c
		effects/snow.c
		effects/prismata.c
		effects/clouds.c

	EMBED_FILES
		${EMBED}/gifs/heart.gif
		${EMBED}/gifs/heart2.gif
		${EMBED}/gifs/boldman.gif
		${EMBED}/gifs/coin.gif
		${EMBED}/gifs/disk.gif
		${EMBED}/gifs/girl.gif
		${EMBED}/gifs/hairyman.gif
		${EMBED}/gifs/hero.gif
		${EMBED}/gifs/torch.gif
		${EMBED}/gifs/zombie.gif
		${EMBED}/gifs/cutebear.gif
)


# The effects read the effect clock instead of the uptime (see effects/effect_time.h)
file(GLOB effect_sources ${CMAKE_CURRENT_SOURCE_DIR}/effects/*.c)
set_source_files_properties(${effect_sources} PROPERTIES
	COMPILE_OPTIONS "-include;${CMAKE_CURRENT_SOURCE_DIR}/effects/effect_time.h")
