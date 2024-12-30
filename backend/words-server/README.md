# Server compilation

```g++ ...```

or

```
cd build
cmake ..
make
```

## Running

in build directory run

```./server 8000```

(or

```cmake .. && make && ./server 8000``` 

if you want to compile every time)

### Play in CLI

To play in command line or debug game you may use this command:

```nc 127.0.0.1 8080```

Firstly, provide your nickname. Than game will guide you by human-readable messages.

### Debug
to run via cmake you may have to change this line

```std::string dictPath = "en_US.dic";```

to this

```std::string dictPath = "../en_US.dic";```

