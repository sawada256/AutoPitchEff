#include "key/ScaleLink.h"
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>
int main() {
    ape::ScaleLink a,b;
    const auto now = ape::ScaleLink::nowNanos();
    if (!a.available() || !b.available() || !a.publish(7,5,now)) return 1;
    if (b.read(7,now).key != 5 || b.publish(7,8,now)) return 2;
    if (b.read(6,now).connected || b.read(7,now+4000000000ULL).connected) return 3;
    const pid_t child = fork();
    if (child == 0) {
        ape::ScaleLink reader;
        _exit(reader.read(7,ape::ScaleLink::nowNanos()).key == 5 ? 0 : 1);
    }
    int status = 0; waitpid(child,&status,0); if (!WIFEXITED(status) || WEXITSTATUS(status)!=0) return 4;
    a.release(); if (b.read(7,now).connected || !b.publish(7,18,now) || a.read(7,now).key != 18) return 5;
    b.release();
    const pid_t crashed = fork();
    if (crashed == 0) { ape::ScaleLink writer; _exit(writer.publish(7,9,ape::ScaleLink::nowNanos()) ? 0 : 1); }
    waitpid(crashed,&status,0);
    if (!WIFEXITED(status) || WEXITSTATUS(status)!=0 || !a.publish(7,4,ape::ScaleLink::nowNanos())) return 6;
    std::cout << "Shared scale: publication, group isolation, conflict, expiry, process IPC, release and dead-owner recovery passed\n";
}
