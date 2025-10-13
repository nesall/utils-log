Header-only, thread-safe and cross-platform logging utility.

Logs into two files:  
* `output.log` (general purpose logging)  
* `diagnostics.log` (crash point logging based on function/block scopes)  


Simple usage:

```cpp
void foo() {
  LOG_START;
  LOG_MSG << "Doing stuff" << 123 << true;
  if (g()) {
    LOG_START1("if block");
  }
  LOG_HERE("Halfway");
}

int main() {
  foo();
  LOG_MSGNF << "This only logs to console";
}
```
