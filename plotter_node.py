import rclpy
from rclpy.node import Node
from std_msgs.msg import String
import matplotlib.pyplot as plt
import re

class PlotterNode(Node):
    def __init__(self):
        super().__init__('plotter_node')
        self.subscription = self.create_subscription(
            String,
            'sensor_data',
            self.listener_callback,
            10
        )
        self.data = []
        self.timer = self.create_timer(5.0, self.plot_data)

    def listener_callback(self, msg):
        # Extraer número de temperatura
        temp = int(re.findall(r'\d+', msg.data)[0])
        self.data.append(temp)
        self.get_logger().info(f'Dato almacenado: {temp}')

    def plot_data(self):
        if len(self.data) == 0:
            return

        plt.figure()
        plt.plot(self.data)
        plt.title("Temperatura vs Tiempo")
        plt.xlabel("Muestras")
        plt.ylabel("Temperatura (°C)")
        plt.savefig('/ros2_ws/data/sensor_plot.png')
        plt.close()

        self.get_logger().info("Gráfico guardado")

def main(args=None):
    rclpy.init(args=args)
    node = PlotterNode()
    rclpy.spin(node)
    node.destroy_node()
    rclpy.shutdown()

if __name__ == '__main__':
    main()
