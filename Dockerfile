# Use latest NVIDIA NGC PyTorch image with CUDA support
FROM nvcr.io/nvidia/pytorch:26.01-py3

# Set working directory
WORKDIR /code

# Set environment variables
ENV PYTHONPATH=/code/src:$PYTHONPATH

# Install system dependencies for development
RUN apt-get update && apt-get install -y \
    wget \
    curl \
    vim \
    nano \
    htop \
    tmux \
    tree \
    openssh-server \
    sudo \
    git \
    && rm -rf /var/lib/apt/lists/*

# Install VS Code Server dependencies
RUN apt-get update && apt-get install -y \
    ca-certificates \
    libc6-dev \
    && rm -rf /var/lib/apt/lists/*

# Copy requirements first for better Docker layer caching
COPY requirements.txt ./

# Copy source code (needed for editable install)
COPY src ./src

# Install Python dependencies
RUN pip install --no-cache-dir -r requirements.txt


# Keep container running for VS Code Remote
CMD ["sleep", "infinity"]
