# Google Maps + Currency Converter Web Application
Application for Computer Science course

This is a currency converter web application with the frontend and a backend.  The frontend consists of three static assets (an `index.html` file, a `styles.css` file, and a `scripts.js` file) and the backend consists of a compiled executable app written C++ that not only has the main backend logic driving the app, but is also the web server serving the app and handling requests to the apps (and also requests from the backend to the currency API).  The app has a Google Maps GUI and the currency conversion form appears on the info window on the map.

The C++ code depends on Boost.Beast (https://github.com/boostorg/beast ), Jinja2Cpp (github.com/flexferrum/Jinja2Cpp ), and Nlohmann.JSON (github.com/nlohmann/json/ ).  The version of Boost used is 1.68.0.  The Beast library is used for the server and client code; Jinja2Cpp is to create an HTML template in `index.html` (it is the C++ implementation of the Jinja2 HTML template library for Python), and the Nlohmann.JSON file is for JSON parsing (the data from currency API comes in the form of JSON data).  

This application has two environment variables declared in the C++ code.  Without them, the application won't work correctly.  Those environment variables are the Google Maps API Key and the currencylayer.com currency API Access Key.  
