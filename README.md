I have not even read a single line of this code. It has been completely generated with Cursor (except for this specific line in the readme. I will modify this in case I write any code myself.)
# Image Annotator

A simple image annotation tool for GNOME that allows you to draw on images and add text annotations.

## Features

- Load images from clipboard or file
- Draw on images with customizable pen width and color
- Add text annotations with customizable font, size, and color
- Save annotated images

## Dependencies

- GTK+ 3.0
- Cairo
- GCC
- pkg-config

## Installation

1. Make sure you have the required dependencies installed:
```bash
sudo zypper install gtk3-devel cairo-devel gcc pkg-config
```

2. Clone this repository or download the source files

3. Compile the program:
```bash
make
```

## Usage

1. Run the program:
```bash
./image_annotator [image_file]
```

If no image file is specified, the program will try to load an image from the clipboard.

2. Use the toolbar to:
   - Open a new image
   - Save the annotated image
   - Choose pen color
   - Adjust pen width
   - Select font for text annotations
   - Toggle between drawing and text mode

3. Draw on the image by clicking and dragging with the mouse
4. Add text by clicking in text mode
5. Save your work using the Save button

## License

This project is licensed under the MIT License. 