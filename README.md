# Predecoded tail-recursive Lama interpreter

## Example usage with Nix devShell

```sh
nix develop
list-tests          # list regression and malformed-input tests
test-all            # run all tests
test-one test001    # run one reference-output comparison
bench               # compare performance on all examples
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
