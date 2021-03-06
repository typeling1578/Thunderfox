# mypy: allow-untyped-defs

import subprocess
from functools import partial
from typing import Callable

from mozlog import get_default_logger

from wptserve.utils import isomorphic_decode

logger = None

def vcs(bin_name: str) -> Callable[..., None]:
    def inner(command, *args, **kwargs):
        global logger

        if logger is None:
            logger = get_default_logger("vcs")

        repo = kwargs.pop("repo", None)
        log_error = kwargs.pop("log_error", True)
        stdout = kwargs.pop("stdout", None)
        stdin = kwargs.pop("stdin", None)
        if kwargs:
            raise TypeError(kwargs)

        args = list(args)

        proc_kwargs = {}
        if repo is not None:
            # Make sure `cwd` is str type to work in different sub-versions of Python 3.
            # Before 3.8, bytes were not accepted on Windows for `cwd`.
            proc_kwargs["cwd"] = isomorphic_decode(repo)
        if stdout is not None:
            proc_kwargs["stdout"] = stdout
        if stdin is not None:
            proc_kwargs["stdin"] = stdin

        command_line = [bin_name, command] + args
        logger.debug(" ".join(command_line))
        try:
            func = subprocess.check_output if not stdout else subprocess.check_call
            return func(command_line, stderr=subprocess.STDOUT, **proc_kwargs)
        except OSError as e:
            if log_error:
                logger.error(e)
            raise
        except subprocess.CalledProcessError as e:
            if log_error:
                logger.error(e.output)
            raise
    return inner

git = vcs("git")
hg = vcs("hg")


def bind_to_repo(vcs_func, repo, log_error=True):
    return partial(vcs_func, repo=repo, log_error=log_error)


def is_git_root(path, log_error=True):
    try:
        rv = git("rev-parse", "--show-cdup", repo=path, log_error=log_error)
    except subprocess.CalledProcessError:
        return False
    return rv == b"\n"
