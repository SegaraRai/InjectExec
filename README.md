# InjectExec
A simple CLI tool for Windows to start a process with a DLL injected.

## Usage

```
InjectExec[32/64].exe [/-R] [/-S] [/-W] <injectee.exe> <injectant.dll> [...arguments for injectee.exe]
```

### Options

#### /-R

  Disable resolving DLL file path to an absolute path.  
  (DLL file path will be converted to an absolute path by default.)

#### /-S

  Disable suspending the main thread of the target process during injection.  
  (The main thread of the target process will be suspended during injection by default.)

#### /-W

  Disable waiting for the target process to finish.  
  (InjectExec waits for the target process to finish by default.)
