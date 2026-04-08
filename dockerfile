FROM osrf/ros:jazzy-desktop

ENTRYPOINT ["/ros_entrypoint.sh"]
CMD ["bash"]

RUN apt update && apt upgrade -y && \
    apt install -y python3-colcon-common-extensions nano

RUN mkdir -p /ros2_ws/src

RUN /bin/bash -c "source /opt/ros/jazzy/setup.bash && \
    cd /ros2_ws/src && \
    ros2 pkg create --build-type ament_python sensor_program --license MIT"

WORKDIR /ros2_ws
