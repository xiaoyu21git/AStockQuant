#!/usr/bin/env python3
import os
import sys

def find_qml_files(root_dir, relative_to):
    qml_files = []
    for root, dirs, files in os.walk(root_dir):
        for file in files:
            if file.endswith('.qml'):
                full_path = os.path.join(root, file)
                # 计算相对于relative_to的路径
                rel_path = os.path.relpath(full_path, relative_to)
                # 将Windows路径分隔符转换为Unix风格
                rel_path = rel_path.replace('\\', '/')
                qml_files.append(rel_path)
    return sorted(qml_files)

def find_js_files(root_dir, relative_to):
    js_files = []
    for root, dirs, files in os.walk(root_dir):
        for file in files:
            if file.endswith('.js'):
                full_path = os.path.join(root, file)
                rel_path = os.path.relpath(full_path, relative_to)
                rel_path = rel_path.replace('\\', '/')
                js_files.append(rel_path)
    return sorted(js_files)

def find_icon_files(root_dir, relative_to):
    icon_files = []
    for root, dirs, files in os.walk(root_dir):
        for file in files:
            if file.endswith('.svg') or file.endswith('.png'):
                full_path = os.path.join(root, file)
                rel_path = os.path.relpath(full_path, relative_to)
                rel_path = rel_path.replace('\\', '/')
                icon_files.append(rel_path)
    return sorted(icon_files)

def generate_qrc(qml_files, js_files, icon_files):
    lines = ['<RCC version="1.0">', '    <qresource prefix="/">']
    
    # 添加图标文件
    for icon_file in icon_files:
        if 'resources/icons' in icon_file:
            lines.append(f'        <file>{icon_file}</file>')
    
    # 添加QML文件
    for qml_file in qml_files:
        lines.append(f'        <file>{qml_file}</file>')
    
    # 添加JS文件
    for js_file in js_files:
        lines.append(f'        <file>{js_file}</file>')
    
    lines.append('    </qresource>')
    lines.append('</RCC>')
    return '\n'.join(lines)

def main():
    current_dir = os.path.dirname(os.path.abspath(__file__))
    qml_dir = os.path.join(current_dir, 'src/app/Qml')
    
    if not os.path.exists(qml_dir):
        print(f"Error: QML directory not found at {qml_dir}")
        sys.exit(1)
    
    # 查找文件
    qml_files = find_qml_files(qml_dir, qml_dir)
    js_files = find_js_files(qml_dir, qml_dir)
    icon_files = find_icon_files(qml_dir, qml_dir)
    
    print(f"Found {len(qml_files)} QML files")
    print(f"Found {len(js_files)} JS files")
    print(f"Found {len(icon_files)} icon files")
    
    # 生成qrc内容
    qrc_content = generate_qrc(qml_files, js_files, icon_files)
    
    # 写入文件
    output_path = os.path.join(qml_dir, 'qml.qrc')
    with open(output_path, 'w', encoding='utf-8') as f:
        f.write(qrc_content)
    
    print(f"Generated qrc file at {output_path}")
    
    # 显示一些示例
    print("\nFirst 10 QML files:")
    for qml in qml_files[:10]:
        print(f"  {qml}")
    
    print("\nFirst 10 icon files:")
    for icon in icon_files[:10]:
        print(f"  {icon}")

if __name__ == '__main__':
    main()