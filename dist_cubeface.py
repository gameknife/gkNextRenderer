import numpy as np
import matplotlib.pyplot as plt
from matplotlib.collections import LineCollection
from matplotlib.widgets import Slider
import sys
from itertools import product, combinations

def generate_points_on_cubeface(num_points, face='front', shuffle=True):
    """
    Generate uniformly distributed points on a specified face of a cube.
    
    Parameters:
    num_points (int): Number of points to generate
    face (str): Which face of the cube to sample ('front', 'back', 'left', 'right', 'top', 'bottom')
    shuffle (bool): Whether to randomly shuffle the points for better small subset distribution
    
    Returns:
    ndarray: Array of shape (num_points, 3) containing the generated 3D points
    """
    # Generate uniform points on a 2D square [0,1]×[0,1]
    # Use Hammersley sequence for better distribution
    points_2d = np.zeros((num_points, 2))
    
    for i in range(num_points):
        # Van der Corput sequence for first dimension
        base = 2
        vdc = 0
        denom = 1
        n = i
        while n > 0:
            denom *= base
            n, remainder = divmod(n, base)
            vdc += remainder / denom
        
        points_2d[i, 0] = vdc
        points_2d[i, 1] = i / num_points  # Second dimension uses simple division
    
    # Map 2D points to the specified cube face (in 3D space)
    points_3d = np.zeros((num_points, 3))
    
    if face == 'front':
        points_3d[:, 0] = 2 * points_2d[:, 0] - 1  # x: [-1, 1]
        points_3d[:, 1] = 2 * points_2d[:, 1] - 1  # y: [-1, 1]
        points_3d[:, 2] = 1                       # z: 1 (front face)
    elif face == 'back':
        points_3d[:, 0] = 2 * points_2d[:, 0] - 1  # x: [-1, 1]
        points_3d[:, 1] = 2 * points_2d[:, 1] - 1  # y: [-1, 1]
        points_3d[:, 2] = -1                      # z: -1 (back face)
    elif face == 'right':
        points_3d[:, 0] = 1                       # x: 1 (right face)
        points_3d[:, 1] = 2 * points_2d[:, 0] - 1  # y: [-1, 1]
        points_3d[:, 2] = 2 * points_2d[:, 1] - 1  # z: [-1, 1]
    elif face == 'left':
        points_3d[:, 0] = -1                      # x: -1 (left face)
        points_3d[:, 1] = 2 * points_2d[:, 0] - 1  # y: [-1, 1]
        points_3d[:, 2] = 2 * points_2d[:, 1] - 1  # z: [-1, 1]
    elif face == 'top':
        points_3d[:, 0] = 2 * points_2d[:, 0] - 1  # x: [-1, 1]
        points_3d[:, 1] = 1                       # y: 1 (top face)
        points_3d[:, 2] = 2 * points_2d[:, 1] - 1  # z: [-1, 1]
    elif face == 'bottom':
        points_3d[:, 0] = 2 * points_2d[:, 0] - 1  # x: [-1, 1]
        points_3d[:, 1] = -1                      # y: -1 (bottom face)
        points_3d[:, 2] = 2 * points_2d[:, 1] - 1  # z: [-1, 1]
    else:
        raise ValueError(f"Unknown face: {face}")
    
    # Shuffle the points for better small subset distribution
    if shuffle:
        np.random.shuffle(points_3d)
    
    return points_3d

def plot_points_2d_interactive(points, face='front'):
    """Plot the points in 2D projection with a slider to control the number of points shown"""
    # Define which coordinates to plot based on the face
    if face in ['front', 'back']:
        x_coord, y_coord = 0, 1  # x, y
    elif face in ['left', 'right']:
        x_coord, y_coord = 1, 2  # y, z
    elif face in ['top', 'bottom']:
        x_coord, y_coord = 0, 2  # x, z
    
    # Draw the boundary of the face
    if face in ['front', 'back']:
        boundary = [(-1, -1), (1, -1), (1, 1), (-1, 1), (-1, -1)]
    elif face in ['left', 'right']:
        boundary = [(-1, -1), (1, -1), (1, 1), (-1, 1), (-1, -1)]
    elif face in ['top', 'bottom']:
        boundary = [(-1, -1), (1, -1), (1, 1), (-1, 1), (-1, -1)]
    
    boundary_x, boundary_y = zip(*boundary)
    
    # Create the figure with a larger height to accommodate the slider
    fig, ax = plt.subplots(figsize=(8, 9))
    
    # Adjust the main plot area to make room for the slider
    plt.subplots_adjust(bottom=0.15)
    
    # Create the initial scatter plot
    num_points = len(points)
    scatter = ax.scatter(points[:1, x_coord], points[:1, y_coord], s=10, alpha=0.7, c='blue')
    boundary_line, = ax.plot(boundary_x, boundary_y, 'k-')
    
    ax.set_aspect('equal')
    ax.set_title(f'Points distribution on {face} face (showing 1/{num_points} points)')
    ax.set_xlim(-1.1, 1.1)
    ax.set_ylim(-1.1, 1.1)
    ax.grid(True, linestyle='--', alpha=0.7)
    
    # Add point indices annotation for the first few points
    annotations = []
    for i in range(min(1, num_points)):
        ann = ax.annotate(str(i), (points[i, x_coord], points[i, y_coord]), 
                          xytext=(5, 5), textcoords='offset points')
        annotations.append(ann)
    
    # Create the slider axis and slider
    ax_slider = plt.axes([0.2, 0.05, 0.65, 0.03], facecolor='lightgoldenrodyellow')
    slider = Slider(ax_slider, 'Num Points', 1, num_points, valinit=1, valstep=1)
    
    # Update function for the slider
    def update(val):
        num_visible = int(slider.val)
        scatter.set_offsets(points[:num_visible, [x_coord, y_coord]])
        
        # Update colors based on index
        colors = plt.cm.viridis(np.linspace(0, 1, num_visible))
        scatter.set_color(colors)
        
        # Update annotations
        for ann in annotations:
            ann.remove()
        annotations.clear()
        
        # Only annotate points if there aren't too many
        if num_visible <= 20:  # Limit annotations to avoid clutter
            for i in range(num_visible):
                ann = ax.annotate(str(i), (points[i, x_coord], points[i, y_coord]), 
                                  xytext=(5, 5), textcoords='offset points')
                annotations.append(ann)
        
        ax.set_title(f'Points distribution on {face} face (showing {num_visible}/{num_points} points)')
        fig.canvas.draw_idle()
    
    # Connect the update function to the slider
    slider.on_changed(update)
    
    plt.show()

def plot_points_2d(points, face='front'):
    """Plot the points in 2D projection based on the face (without interactivity)"""
    fig, ax = plt.subplots(figsize=(8, 8))
    
    # Define which coordinates to plot based on the face
    if face in ['front', 'back']:
        x_coord, y_coord = 0, 1  # x, y
    elif face in ['left', 'right']:
        x_coord, y_coord = 1, 2  # y, z
    elif face in ['top', 'bottom']:
        x_coord, y_coord = 0, 2  # x, z
    
    ax.scatter(points[:, x_coord], points[:, y_coord], s=10, alpha=0.7)
    
    # Draw the boundary of the face
    if face in ['front', 'back']:
        boundary = [(-1, -1), (1, -1), (1, 1), (-1, 1), (-1, -1)]
    elif face in ['left', 'right']:
        boundary = [(-1, -1), (1, -1), (1, 1), (-1, 1), (-1, -1)]
    elif face in ['top', 'bottom']:
        boundary = [(-1, -1), (1, -1), (1, 1), (-1, 1), (-1, -1)]
    
    boundary_x, boundary_y = zip(*boundary)
    ax.plot(boundary_x, boundary_y, 'k-')
    
    ax.set_aspect('equal')
    ax.set_title(f'Points distribution on {face} face')
    ax.set_xlim(-1.1, 1.1)
    ax.set_ylim(-1.1, 1.1)
    plt.grid(True, linestyle='--', alpha=0.7)
    plt.show()

def plot_points_3d(points):
    """Plot the points in 3D"""
    fig = plt.figure(figsize=(10, 10))
    ax = fig.add_subplot(111, projection='3d')
    
    # Plot the points
    ax.scatter(points[:, 0], points[:, 1], points[:, 2], s=10, alpha=0.7)
    
    # Draw the cube wireframe
    r = [-1, 1]
    for s, e in combinations(np.array(list(product(r, r, r))), 2):
        if np.sum(np.abs(s-e)) == 2:
            ax.plot3D(*zip(s, e), color="black", alpha=0.3)
    
    ax.set_xlabel('X')
    ax.set_ylabel('Y')
    ax.set_zlabel('Z')
    ax.set_title('3D Distribution of points on cube face')
    ax.set_xlim(-1.1, 1.1)
    ax.set_ylim(-1.1, 1.1)
    ax.set_zlim(-1.1, 1.1)
    plt.show()

def normalize_vectors(points):
    """Normalize vectors to unit length"""
    norms = np.sqrt(np.sum(points**2, axis=1))
    return points / norms[:, np.newaxis]

def save_points_to_file(points, output_file, num_points):
    """Save the points to a text file in C++ vec3 array format"""
    # Normalize the vectors before saving
    normalized_points = normalize_vectors(points)
    
    with open(output_file, 'w') as f:
        f.write(f"const vec3 hemisphereVectors[{num_points}] = {{\n")
        
        for i, point in enumerate(normalized_points):
            f.write(f"    vec3({point[0]}, {point[1]}, {point[2]})")
            
            # Add comma except for the last element
            if i < num_points - 1:
                f.write(",\n")
            else:
                f.write("\n")
                
        f.write("};\n")

if __name__ == "__main__":
    # Parse command line arguments
    if len(sys.argv) < 3:
        print("Usage: python dist_cubeface.py <num_points> <output_file> [face]")
        print("Available faces: front, back, left, right, top, bottom")
        sys.exit(1)
    
    num_points = int(sys.argv[1])
    output_file = sys.argv[2]
    face = sys.argv[3] if len(sys.argv) > 3 else 'front'
    
    # Generate points
    points = generate_points_on_cubeface(num_points, face, shuffle=True)
    
    # Save points to file (will normalize inside the function)
    save_points_to_file(points, output_file, num_points)
    
    # Visualize with interactive slider
    plot_points_2d_interactive(points, face)
    
    # Uncommment to visualize in 3D
    # plot_points_3d(points)
    
    print(f"Generated {num_points} points on the {face} face of the cube and saved normalized vectors to {output_file}")