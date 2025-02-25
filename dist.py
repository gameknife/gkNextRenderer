import numpy as np
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D
import plotly.graph_objects as go

# 生成半球均匀分布向量
num_points = 16
indices = np.arange(0, num_points, dtype=float) + 0.5

phi = np.arccos(1 - indices/(1*num_points))  # 极角均匀分布, 限制在90度
theta = np.pi * (1 + 5**0.5) * indices     # 方位角黄金比例分布

x = np.sin(phi) * np.cos(theta)
y = np.sin(phi) * np.sin(theta)
z = np.cos(phi)

# 创建可视化
fig = go.Figure(data=[go.Scatter3d(
    x=x,
    y=y,
    z=z,
    mode='markers',
    marker=dict(
        size=5,
        color=z,
        colorscale='Viridis',
        opacity=0.8
    )
)])

fig.update_layout(
    scene=dict(
        xaxis=dict(showbackground=False, range=[-1.2,1.2]),
        yaxis=dict(showbackground=False, range=[-1.2,1.2]),
        zaxis=dict(showbackground=False, range=[0,1.2]),
        aspectmode='manual',  # 切换为手动模式
        aspectratio=dict(
            x=1,   # x轴基准比例
            y=1,   # y轴与x轴等比例
            z=0.5  # z轴比例为x轴的50%（因数据范围是x轴的一半）
        )
    ),
    margin=dict(l=0, r=0, b=0, t=0),
    title='修正比例的半球面分布'
)

# 保存向量和可视化
np.savetxt("hemisphere_vectors.csv", np.column_stack((x, y, z)), delimiter=",")
fig.write_html("hemisphere_visualization.html")

# 将向量转换为glm::vec3的C++数组
cpp_array = "const glm::vec3 hemisphereVectors[{}] = {{\n".format(num_points)
for i in range(num_points):
    cpp_array += "    glm::vec3({}, {}, {}),\n".format(x[i], y[i], z[i])
cpp_array = cpp_array.rstrip(",\n") + "\n};"

print(cpp_array)

# 保存向量到头文件
header_file_content = """#ifndef HEMISPHERE_VECTORS_H
#define HEMISPHERE_VECTORS_H

#include <glm/glm.hpp>

{}

#endif
""".format(cpp_array)

with open("hemisphere_vectors.h", "w") as f:
    f.write(header_file_content)

print("hemisphere_vectors.h 文件已生成")