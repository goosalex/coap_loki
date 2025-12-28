# Motor driver selection logic
set(MOTOR_DRIVER_SOURCE "motor_dummy.c")  # Default to dummy

# Select motor driver based on board configuration
if(CONFIG_MOTOR_DRIVER_TB67)
    set(MOTOR_DRIVER_SOURCE "motorTB67driver.c")
    message(STATUS "Using TB67 motor driver")
elseif(CONFIG_MOTOR_DRIVER_DRV8871)
    set(MOTOR_DRIVER_SOURCE "motor_drv8871.c") 
    message(STATUS "Using DRV8871 motor driver")
else()
    message(STATUS "Using dummy motor driver (no-op)")
endif()

# Add the selected driver source file
target_sources(app PRIVATE ${MOTOR_DRIVER_SOURCE})

# Add motor interface header
target_include_directories(app PRIVATE .)