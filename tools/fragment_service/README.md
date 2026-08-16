# Protected fragment evaluator

This directory belongs inside the private Asian engine. Do not copy it or the
engine into the external assistant's workspace.

The service accepts one `candidate_fragment.inc`, compiles and runs it against
the public harness in a Bubblewrap filesystem/network sandbox, then returns
only parsed metrics. Compiler output and generated binaries are never returned.
The private engine baseline runs in a separate trusted process, so candidate
code never shares its address space or filesystem view.

## Prepare

```sh
cd /home/pablo/Projects/asian-option-engine
make -j"$(nproc)"
python3 -c 'import secrets; print(secrets.token_urlsafe(32))' > fragment-token
chmod 600 fragment-token
```

Keep `fragment-token` private and untracked.

Confirm Bubblewrap works on the builder before accepting delegated code:

```sh
bwrap --ro-bind /usr /usr --ro-bind /etc /etc \
  --symlink usr/bin /bin --symlink usr/lib /lib --symlink usr/lib /lib64 \
  --proc /proc --dev /dev --tmpfs /tmp --unshare-all --new-session \
  /usr/bin/true
```

## Start on the private AVX-512 machine

```sh
python3 tools/fragment_service/server.py \
  --listen 127.0.0.1 \
  --port 8765 \
  --token-file fragment-token
```

Bind to loopback and expose it only through an SSH tunnel. Do not use
`--unsafe-local-evaluator` for external submissions; that switch exists solely
to test the plumbing in an already isolated disposable environment.

## Security boundary

- The candidate sandbox contains only the public harness and submitted include.
- It has no network namespace and no private source mount.
- The complete private binary stays on the builder.
- Responses contain whitelisted numeric fields only.
- Final insertion into the Asian hot loop is still a deliberate manual review.

This is practical compartmentalization, not a claim that arbitrary native code
can be made risk-free. Run the service under a dedicated unprivileged account or
VM and keep the private repository unreadable by the external assistant's
account.
