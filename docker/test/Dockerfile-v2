# Use a lightweight base image
FROM alpine:latest

# Set working directory
WORKDIR /app

# Create a simple Hello World script
RUN echo 'echo "Hello, World!"' > hello.sh

# Make the script executable
RUN chmod +x hello.sh

# Set the default command to run the script
CMD ["./hello.sh"]