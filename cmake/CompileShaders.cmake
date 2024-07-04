find_package(Vulkan 1.3 COMPONENTS glslc)
find_program(glslc_executable NAMES glslc HINTS Vulkan::glslc)

function(compile_shaders target)
    get_target_property(target_sources ${target} SOURCES)

    foreach(source ${target_sources})
        get_filename_component(fname ${source} NAME)
        set(spirv "${CMAKE_SOURCE_DIR}/${fname}.spv") 
        add_custom_command(
            OUTPUT ${spirv}
            DEPENDS ${source}
            COMMAND
                ${glslc_executable}
                -o ${spirv}
                ${PROJECT_SOURCE_DIR}/${source}
            VERBATIM
        )
        list(APPEND target_spirvs ${spirv})
    endforeach()

    target_sources(${target} PRIVATE ${target_spirvs})
endfunction()