# Iris Slide Python API
[![Conda Version](https://img.shields.io/conda/vn/conda-forge/iris-slide.svg?style=for-the-badge&logo=anaconda)](https://anaconda.org/conda-forge/iris-slide) 
[![PyPI Version](https://img.shields.io/pypi/v/Iris-Slide?color=blue&style=for-the-badge&logo=pypi)](https://pypi.org/project/Iris-Slide/)
[![Python CI](https://img.shields.io/github/actions/workflow/status/Aptix-Bio/Iris-Slide/distribute-pypi.yml?style=for-the-badge&logo=python&label=Python%20CI)](https://github.com/Aptix-Bio/Iris-Slide/actions/workflows/python-CI.yml)

Copyright &copy; 2025 Iris Developers; MIT Software License

The Iris Slide Community module is a part of the Iris Digital Pathology project. This module allows for:

- Reading and writing of Iris whole slide image (WSI) digital slide files (.iris)
- Decoding JPEG-compressed tile image data, and JPEG or AVIF associated images.

This module was designed to allow for extremely fast slide access using a simple API. We want to simplify access to these files for you.

Iris Slide for Python is available via the Anaconda and PyPi package managers. We prefer the Anaconda enviornment as it includes dynamic libraries. In a true package manager sense, if you choose to develop C/C++ applications with Python bindings Anaconda will allow you to dynamically link the C++ Iris-Slide in addition to Python modules.

## Installation

### Conda-Forge (Recommended)
[![Static Badge](https://img.shields.io/badge/Feedstock-Iris_Slide-g?style=for-the-badge)](https://github.com/conda-forge/Iris-Slide-feedstock) 
[![Conda Downloads](https://img.shields.io/conda/dn/conda-forge/iris-slide.svg?style=for-the-badge)](https://anaconda.org/conda-forge/iris-slide) 
[![Conda Platforms](https://img.shields.io/conda/pn/conda-forge/iris-slide.svg?color=blue&style=for-the-badge)](https://anaconda.org/conda-forge/iris-slide)

We prefer the Anaconda environment as it includes optimized dynamic libraries and better dependency management for scientific computing environments.

**Quick Installation:**
```shell
conda install -c conda-forge iris-slide
```

**Or with mamba (faster):**
```shell
mamba install iris-slide
```

**Environment Setup:**
```shell
# Configure conda-forge channel permanently
conda config --add channels conda-forge
conda config --set channel_priority strict

# Then install
conda install iris-slide
```

### PyPI
[![PyPI - Status](https://img.shields.io/pypi/status/iris-slide?style=for-the-badge)](https://pypi.org/project/Iris-Slide/)
[![PyPI - Python Version](https://img.shields.io/pypi/pyversions/Iris-Slide?style=for-the-badge)](https://pypi.org/project/Iris-Slide/)
[![PyPI - Wheel](https://img.shields.io/pypi/wheel/iris-slide?style=for-the-badge)](https://pypi.org/project/Iris-Slide/)
[![PyPI - Downloads](https://img.shields.io/pepy/dt/iris-slide?style=for-the-badge)](https://pypi.org/project/Iris-Slide/)
[![Supported Platforms](https://img.shields.io/badge/platforms-Many%20Linux%20%7C%20macOS%20%7C%20Windows-blue?style=for-the-badge)](https://pypi.org/project/Iris-Slide/)

**Basic Installation:**
```shell
pip install iris-slide
```

The encoder is included: it encodes DICOM whole-slide images (JPEG, JPEG 2000 or HTJ2K frames).

## Key Features

- **High-Performance Slide Reading**: Optimized C++ backend with Python bindings
- **Encoding**: Read Iris files and encode DICOM whole-slide images (JPEG, JPEG 2000 or HTJ2K frames)
- **Memory Efficient**: Tiles loaded on-demand with automatic memory management
- **Research-Friendly**: NumPy array output, PIL/Pillow integration
- **Metadata Rich**: Full access to slide properties, annotations, and associated images
- **Encoding Pipeline**: Convert vendor formats (SVS, NDPI, VSI, MRXS) to optimized Iris format
- **Privacy Controls**: Metadata stripping and anonymization for research workflows 

## Quick Start

### Basic Slide Reading

```python
from Iris import Slide

# Open and validate slide
slide_path = 'path/to/slide.iris'
result = Slide.validate_slide_path(slide_path)
if result.failed():
    raise Exception(f'Validation failed: {result.message}')

slide = Slide.open_slide(slide_path)
if not slide:
    raise Exception('Failed to open slide')

# Get slide information
result, info = slide.get_info()
if result.failed():
    raise Exception(f'Failed to read slide info: {result.message}')

print(f"Slide: {info.extent.width}x{info.extent.height}px")
print(f"Layers: {len(info.extent.layers)}")
print(f"Encoding: {info.encoding}")
```

### Slide Encoding

```python
from Iris import Encoder

written = Encoder.encode_slide_file(source='input.dcm', outdir='./output/', layer_step=2, concurrency=8)
print(f'Wrote {written}')   # raises RuntimeError on failure
```

## API Reference

### Core Reading Functions

Import the Iris Slide module and basic validation:

```python
from Iris import Slide

# File validation (recommended before opening)
slide_path = 'path/to/slide_file.iris'
result = Slide.validate_slide_path(slide_path)
if result.failed():
    raise Exception(f'Invalid slide file: {result.message}')
print(f"Slide '{slide_path}' passed validation")
```

### Opening Slides

Deep validation performs comprehensive checks of the internal offset chain and IFE standard compliance:

```python
# Open slide file
slide = Slide.open_slide(slide_path)
if not slide: 
    raise Exception('Failed to open slide file')
```

### Slide Information and Metadata

Get comprehensive slide information including dimensions, layers, and metadata:

```python
# Get slide abstraction
result, info = slide.get_info()
if result.failed():
    raise Exception(f'Failed to read slide information: {result.message}')

# Basic slide properties
extent = info.extent
print(f"Slide: {extent.width}x{extent.height}px")
print(f"Encoding: {info.encoding}")
print(f"Format: {info.format}")

# Layer information
print(f"Layers: {len(extent.layers)}")
for i, layer in enumerate(extent.layers):
    print(f"  Layer {i}: {layer.x_tiles}x{layer.y_tiles} tiles, {layer.scale}x scale, {layer.downsample}x downsample")

# Metadata access
metadata = info.metadata
print(f"Codec Version: {metadata.codec_version.major}.{metadata.codec_version.minor}.{metadata.codec_version.build}")
print(f"Microns Per Pixel: {metadata.microns_per_pixel}")
print(f"Magnification: {metadata.magnification}")

# Slide attributes
print("Slide attributes:")
for key in metadata.attributes:
    value = metadata.attributes[key]
    print(f"  {key}: {value}")
```

### Reading Tile Data

Read individual tiles as NumPy arrays for analysis:

```python
import numpy as np
from PIL import Image

# Read a specific tile
layer_index = 0  # Lowest resolution layer
tile_index = 0   # First tile

# Get tile as NumPy array (RGBA format)
tile_array = slide.read_slide_tile(layer_index, tile_index)
print(f"Tile shape: {tile_array.shape}")  # Should be (256, 256, 4)
print(f"Tile dtype: {tile_array.dtype}")  # Should be uint8

# Convert to PIL Image for display/processing
tile_image = Image.fromarray(tile_array)
tile_image.show()

# Access raw pixel data
red_channel = tile_array[:, :, 0]
alpha_channel = tile_array[:, :, 3]
```

### Creating Composite Images

Generate overview images by stitching tiles together:

```python
from PIL import Image

def create_layer_composite(slide, layer_index=0):
    """Create a composite image from all tiles in a layer"""
    
    # Get layer information
    result, info = slide.get_info()
    if result.failed():
        raise Exception(f'Failed to read slide info: {result.message}')
    
    layer = info.extent.layers[layer_index]
    scale = int(layer.scale)
    
    # Create composite image
    composite_width = info.extent.width // scale
    composite_height = info.extent.height // scale
    composite = Image.new('RGBA', (composite_width, composite_height))
    
    # Stitch tiles together
    tile_size = 256  # Iris tiles are always 256x256
    for y in range(layer.y_tiles):
        for x in range(layer.x_tiles):
            tile_index = y * layer.x_tiles + x
            tile_array = slide.read_slide_tile(layer_index, tile_index)
            tile_image = Image.fromarray(tile_array)
            
            # Paste tile into composite
            paste_x = x * tile_size
            paste_y = y * tile_size
            composite.paste(tile_image, (paste_x, paste_y))
    
    return composite

# Create and display low-resolution overview
overview = create_layer_composite(slide, layer_index=0)
overview.show()
```

> [!CAUTION]
> **Memory Warning**: Despite Iris's fast read speeds, creating composite images of high-resolution layers requires substantial memory and processing time. PIL.Image creates full in-memory images rather than tiled representations. Limit composite creation to layer 0 or 1 for practical performance.

### Associated Images and Thumbnails

Access embedded thumbnails and associated images:

```python
from PIL import Image

# Get slide info first
result, info = slide.get_info()
if result.failed():
    raise Exception(f'Failed to read slide information: {result.message}')

# List available associated images
print("Available associated images:")
for image_name in info.metadata.associated_images:
    print(f"  - {image_name}")

# Read and display thumbnail if available
if 'thumbnail' in info.metadata.associated_images:
    thumbnail_array = slide.read_associated_image('thumbnail')
    thumbnail_image = Image.fromarray(thumbnail_array)
    thumbnail_image.show()
    print(f"Thumbnail size: {thumbnail_image.size}")

# Read other associated images
if 'label' in info.metadata.associated_images:
    label_array = slide.read_associated_image('label')
    label_image = Image.fromarray(label_array)
    label_image.show()
```

### Advanced Reading Patterns

Efficient patterns for large-scale analysis:


## Encoding API

The Python module encodes a DICOM whole-slide image (its highest-resolution instance; JPEG, JPEG 2000 or HTJ2K frames) as an Iris slide, `<source stem>.jpeg.iris`, or `<source stem>.av1.iris` with `encoding='av1'`. Every lower layer is derived by a 2x bicubic reduction, down to the first that fits one tile. Every failure raises `RuntimeError` naming what failed.

### Encoding

```python
from Iris import Encoder

written = Encoder.encode_slide_file(
    source='input.dcm',
    outdir='./encoded/',
    quality=90,           # JPEG quality, 1-100
    subsampling='422',    # '444', '422' or '420'
    layer_step=4,         # a written layer every 4x
    memory_budget=0,      # bytes held at once; 0 is a quarter of physical memory
    concurrency=8,
)
```

A progress line is printed while it runs (`show_progress=False` turns it off); Ctrl-C interrupts the encode and leaves no file.

### Driving the encoder yourself

```python
import time
from Iris.Encoder import create_encoder, dispatch_encoder, encoder_progress, EncoderState

encoder = create_encoder('input.dcm', './encoded/')
dispatch_encoder(encoder)
progress = encoder_progress(encoder)
while progress.state == EncoderState.ACTIVE:
    time.sleep(0.5)
    progress = encoder_progress(encoder)
print(progress.state, progress.file, progress.error)
```

### Encoding Options Reference

**Tile encodings:** JPEG and AV1 (BT.601 at full range, 8-bit 4:2:0) are encoded. AV1 tiles are not yet decoded, and the UASTC and XUASTC tile encodings are neither encoded nor decoded; reading a slide whose tiles declare one fails with an error naming the encoding.

**Identity:** from the DICOM source only the pixel spacing, magnification and ICC profile are carried. The caller's `i2s_attributes` (with `i2s_version`, `major << 8 | minor`) are the slide's only attributes.

## Integration Examples

### Channel-Separated Data with PyTorch

Iris provides `read_slide_tile_channels` which returns data in `[4,256,256]` format with separate channel arrays, useful for advanced image processing:

```python
import numpy as np
import torch
from Iris import Slide

def create_channel_separated_dataset(slide_path, layer_index=0):
    """Demonstrate channel-separated tile reading with PyTorch tensors"""
    
    slide = Slide.open_slide(slide_path)
    if not slide:
        raise Exception(f'Failed to open slide: {slide_path}')
    
    result, info = slide.get_info()
    if result.failed():
        raise Exception('Failed to read slide info')
    
    layer = info.extent.layers[layer_index]
    print(f"Processing layer {layer_index}: {layer.x_tiles}x{layer.y_tiles} tiles")
    
    # Read first tile using channel-separated format
    tile_channels = slide.read_slide_tile_channels(layer_index, tile_index=0)
    print(f"Channel-separated shape: {tile_channels.shape}")  # Should be [4, 256, 256]
    
    # Convert to PyTorch tensor
    tensor = torch.from_numpy(tile_channels).float()
    
    # Access individual channels
    red_channel = tensor[0]      # Red channel [256, 256]
    green_channel = tensor[1]    # Green channel [256, 256] 
    blue_channel = tensor[2]     # Blue channel [256, 256]
    alpha_channel = tensor[3]    # Alpha channel [256, 256]
    
    print(f"Red channel shape: {red_channel.shape}")
    print(f"Alpha channel mean: {alpha_channel.mean():.2f}")
    
    # Example: Create a custom RGB combination
    # Emphasize red channel, reduce blue
    enhanced_rgb = torch.stack([
        red_channel * 1.2,      # Enhance red
        green_channel,          # Keep green
        blue_channel * 0.8      # Reduce blue
    ], dim=0)
    
    return enhanced_rgb, tensor

# Usage example
enhanced_rgb, original_tensor = create_channel_separated_dataset('slide.iris', layer_index=1)
print(f"Enhanced RGB shape: {enhanced_rgb.shape}")  # [3, 256, 256]
print(f"Original RGBA shape: {original_tensor.shape}")  # [4, 256, 256]
```

This channel-separated format is particularly useful for:
- **Custom color space transformations**
- **Advanced image augmentations** 
- **Channel-specific analysis** (e.g., studying alpha transparency patterns)
- **Memory-efficient processing** when you only need specific channels

For additional support and examples, visit the [Iris-Slide GitHub repository](https://github.com/Aptix-Bio/Iris-Slide).