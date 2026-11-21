# OS Scheduler Simulator

A GTK-based simulator of an operating system process scheduler, written in C.
It loads programs from text files and simulates their execution with:

- Scheduling: First Come First Serve (FCFS), Round Robin (RR), Multilevel Feedback Queue (MLFQ)
- Memory: a fixed 60-word memory holding each process's PCB, variables and instructions
- Mutual exclusion: `semWait` / `semSignal` on shared resources (user input, user output, file)
- A GUI showing processes, ready/blocked queues, resources, memory and an execution log

Program instructions supported: `print`, `assign`, `writeFile`, `readFile`, `printFromTo`, `semWait`, `semSignal`.

## Build

Requires GCC, `pkg-config` and GTK 3 development headers.

```sh
# Debian/Ubuntu
sudo apt-get install build-essential pkg-config libgtk-3-dev
make
```

## Run

```sh
./scheduler_sim
```

In the GUI, choose the scheduler, set the RR quantum, pick a program file and arrival time for each of the three processes, then use Start / Step / Auto.

## License

MIT, see `LICENSE`.
