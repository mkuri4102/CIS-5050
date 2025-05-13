# Frontend Codebase Documentation
This is the frontend HTTP server implementation for our PennCloud Project.

## Codebase structure

- ```/build``` contains all build ```*.o``` files
- ```/include``` contains all ```*.h``` header files.
- ```/bulid``` contains all ```*.html``` files and ```*.css``` static files for rendering
- ```/src``` contains all the source code and ```*.cpp``` implementation files for the frontend server.
- ```/src/handlers``` contains the handlers for GET, HEAD, and POST requests
- ```/src/routes``` contains all the endpoints implementation.
- ```/src/utils``` contains all the utility file implementations for integration with other components in the codebase.
- ```/src/webstorage``` contains the implementation for the web storage module of the PennCloud application.
- ```http_server.cpp``` contains the implementation for the multithreaded http_servers.
- ```load_balancer.cpp``` contains the implementation for the frontend load balancer.
- ```start_servers.sh``` the bash script that helps you quickly spin up the functioning frontend. It spins up the load balancer and three replicas of the HTTP server.


## Usage
Run ```make run_lb``` on macOS to spin up the frontend.