# Start from the official Zephyr build environment
FROM zephyrprojectrtos/zephyr-build:v0.26-branch
USER root
# Install SDL2 and X11 dependencies (for GUI forwarding)
RUN apt-get update && \
    apt-get install -y \
    libsdl2-dev \
    libx11-dev \
    libxext-dev \
    libxrender-dev \
    && rm -rf /var/lib/apt/lists/*

# Verify SDL2 installation
RUN sdl2-config --version

# Set up a non-root user (optional but recommended)
#RUN useradd -m zephyruser && \
#    echo "zephyruser ALL=(root) NOPASSWD:ALL" >> /etc/sudoers
#USER zephyruser
#WORKDIR /home/zephyruser