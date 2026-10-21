# Contributors

## Issue reports and testing

### [@jason-oliveira](https://github.com/jason-oliveira)

Reported (#1) that a fresh `git clone --recursive` failed: the snesrecomp
submodule was pinned to `ffbd4bf`, a commit no longer on the remote, and the
newest toolkit at the time garbled the intro. That showed the game was pinning commits that weren't safe
to pin. The toolkit branches this build needed are now merged into snesrecomp
`main` with merge commits, and the submodule pins `main`.
