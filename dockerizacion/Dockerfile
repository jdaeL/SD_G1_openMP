FROM gcc:latest

WORKDIR /app

# GCC ya incluye libgomp (OpenMP)
# instalamos 'make' para automatizar el build y 'htop' para que puedas 
# visualizar en consola cómo tus hilos estresan la CPU durante la prueba
RUN apt-get update && apt-get install -y make htop

CMD ["bash"]