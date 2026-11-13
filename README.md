# Inirect-threaded Lama interpreter

## Example usage with Nix devShell

```sh
nix develop
list-tests          # show test names in a Nushell table
test-all            # run tests and render three-VM userspace tables via Nushell
test-one test001    # show timings, outputs, and userspace statistics
bench               # render three-VM benchmark tables via Nushell
bench fib           # run one benchmark
bench --list        # list benchmark names
lamac -h            # show reference compiler options
```

## Manual compilation and execution

```sh
nix develop
make -C packages/lama-vm

lamac -b bench/fib.lama
lama-custom-vm fib.bc
lamavm fib.bc
ocaml-s bench/fib.lama </dev/null
```

## Example usage with Docker

```sh
nix build .#docker
docker load < result
docker run --rm -it dmfrpro/lama-vm
```
