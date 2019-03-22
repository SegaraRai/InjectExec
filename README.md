# InjectExec
A simple CLI tool for Windows to start a process with a DLL injected.

## Usage

```
InjectExec[32/64].exe [/-R] [/-S] <injectee.exe> <injectant.dll> [...arguments for injectee.exe]
```

### Options

#### /-R

  Disable resolving DLL file path to an absolute path.  
  (DLL file path will be converted to an absolute path on default.)

#### /-S

  Disable suspending the main thread of the target process during injection.  
  (The main thread of the target process will be suspended during injection on default.)
