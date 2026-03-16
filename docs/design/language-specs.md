# Maestro Language Specifications

> "Programs must be written for people to read, and only incidentally
> for machines to execute."
>
> – Abelson & Sussman, SICP, preface to the first edition

## Maestro Philosophy

Maestro was designed to be Lisp-like, the only allowed expression form
is S-expressions. Macros and data are the core concepts and states are
special macros.

The runtime is intentionally small. Maestro is not meant to be a
standalone application, it is meant to be embedded. Tools, logging,
printing, network access, timers, storage, and anything fancy should
be bound from the outside by the embedding host.

## File Name

Maestro source files should have the extension `.mstr`. But any
extension of your choice works.

Maestro module bundles should have the extension `.mstro`. But any
extension works as well.

Maestro must be embedded into another program, it is not meant to stand
on its own.

## Comments

Code comments follow Lisp rules.

- `;` starts a comment
- `;;` is preferred for normal comments

Example:

```lisp
;; this is a comment
(define answer 42) ; this is also a comment
```

## Definitions

Declarations are definitions, there are no forward declarations.

### Identifiers

A single identifier must follow this shape:

- leading character: `[a-zA-Z_]`
- non-leading characters: `[a-zA-Z0-9._\-?*+/=!<>]`

Equivalent regular expression:

```text
[a-zA-Z_][a-zA-Z0-9._\-?*+/=!<>]*
```

There's no stopping the user from using Morse Code to encode
identifiers.

Maestro does not support overloading.

### Reserved Literals and Keywords

The following names are reserved and may not be rebound:

- `true`
- `false`
- `empty-string`
- `empty-list`
- `empty-object`
- `default`
- `start`
- `end`
- `last-state`

### Scopes and Shadowing

There are two kinds of scopes in Maestro:

1. Global: the global scope contains all the global definitions.
2. Per-macro scopes: the scope that each macro call creates.

States are special macros, so state execution creates per-macro scope as
well, with extra runtime metadata attached by the VM.

Global definitions in a module are not ordered. Missing global
definitions are checked at parse time.

Definitions inside `steps` are strongly ordered.

Shadowing is explicit via `let`.

`let` may:

- create a new binding
- replace an existing binding in the same scope
- shadow a binding from a parent scope

`set` does not create bindings. It only changes the value of an existing
binding and obeys normal shadowing rules.

## Modules

Modules are defined with the following syntax:

```lisp
(module parent-module ... this-module)
```

A valid Maestro source file must contain exactly one module statement.

Example:

```lisp
(module std strings)
```

### Exports

An identifier can be exported via:

```lisp
(export identifier)
```

All global definitions in the file may be exported via:

```lisp
(export *)
```

Example:

```lisp
(export concat-lines)
(export *)
```

### Imports

Imports are defined using the following syntax:

```lisp
(define local-name (import module-path identifier))
```

Shorthand imports are also allowed:

```lisp
(import module-path identifier)
```

This is shorthand for:

```lisp
(define identifier (import module-path identifier))
```

Wildcard imports are allowed:

```lisp
(import module-path *)
```

This resolves to a set of definitions:

```lisp
(define identifier-1 (import module-path identifier-1))
(define identifier-2 (import module-path identifier-2))
```

Non-aliased imports must check for name collisions.

Subprograms are imported with:

```lisp
(define subprogram (import-program module-path))
```

`import-program` may target a program even if its `start` state was not
exported.

Inline imports are allowed within macro and state definitions.

Inline imports are resolved at parse/link-time and are only visible at
the exact point where they appear. Subsequent statements cannot see
them.

Because inline imports are only ephemerally visible, they are not
considered candidates for naming conflicts.

Examples:

```lisp
(state (next)
  (steps
    (transition (import app states done))))
```

Example:

```lisp
(define substr (import std strings substr))
(import std strings concat)
(define worker (import-program app worker))
```

## Macros

Macros are defined using the following syntax:

```lisp
(define (identifier argument1 ...) (expression))
```

Upon invocation, the macro expands to the given expression.

Macros may be called.

Macros may not be transitioned into.

Macros may be passed as arguments. When passed as arguments, they resolve
to references, effectively C pointers, to the referenced macro.
Passed macro values satisfy both `macro?` and `ref?`.

Example:

```lisp
(define (identity x) x)
```

### Reference Parameters

Macro parameters are passed by value unless explicitly declared as
references.

Reference parameters are written like this:

```lisp
(define (foo (ref a)) (set a 42))
```

In this case `a` is a reference.

Macro call arguments do not require explicit `ref`. Reference passing is
inferred from the callee parameter declaration.

Example:

```lisp
(define (reset-counter (ref counter))
  (set counter 0))

(define (some-macro)
  (steps
    (let var 0)
    (reset-counter var)))
```

### Aliases

Aliases are defined using the following syntax:

```lisp
(define alias-identifier identifier)
```

Example:

```lisp
(define copy concat)
```

## States

States are special macros with extra metadata carried by the VM.

That metadata lets the runtime:

- register a state in the state machine
- enter a state as an execution unit
- perform transitions

States may be transitioned into.

States are not callable as macros.

States may be passed as arguments. When passed as arguments, they
resolve to references, effectively C pointers, to the referenced state.
Passed state values satisfy both `state?` and `ref?`.

State bodies do not return expression values to a caller because states
are transitioned into, not returned from. The state return value is
captured in the data object `last-state` under the field `val`.

States are defined like this:

```lisp
(state (identifier argument1 ...) (steps ...))
```

`end` is reserved and may not be defined as a state.

Example:

```lisp
(state (idle)
  (steps
    (print "waiting")
    (transition idle)))
```

### Program Entry

The starting state of a Maestro program is `start`.

If a state named `start` exists, the module is executable. Otherwise the
module acts as a library.

Transitioning to `end` terminates the program.

When a program transitions to `end`, it must provide a return value.

Program and subprogram return values may be any valid Maestro value.

`last-state` is a reserved runtime data object with two fields:

- `(get last-state val)` returns the captured return value of the
  previous state
- `(get last-state state)` returns a reference to the previous state

When starting a new program, `last-state` is initialized by the runtime
so that its `state` field is `start` and its `val` field is
unspecified. `val` is unspecified because it may hold a value of any
type.

## Core Forms

The core language forms are:

- `define`
- `state`
- `let`
- `set`
- `case`
- `steps`
- `export`
- `import`
- `module`
- `transition`
- `run`

### Steps

`steps` defines sequential execution.

Example:

```lisp
(steps
  (print "hello")
  (print "world"))
```

`steps` is syntactic sugar over sequential execution.

The return value of a `steps` expression is the return value of its last
statement.

### Transition

Transitions are performed using:

```lisp
(transition next-state)
(transition end return-value)
```

If no transition occurs, execution loops back to the same state.

If a transition targets `end`, program execution terminates.

Transitions to `end` must include a return value.

If a `steps` block inside a state definition reaches the end without a
transition, execution loops back to that same state with its persistent
values intact.

If a transition targets a state in another module, program lifetime is
handed to that other module. Control does not return to the original
module unless the new module explicitly transitions back.

Cross-module transitions may target either:

- an inline import
- a previously imported alias

Examples:

```lisp
(transition idle)
(transition end 0)
(transition end "done")
(transition (import app states done))
```

### run

`run` executes the state machine defined in another module.

Syntax:

```lisp
(run subprogram args)
```

The target must be imported with `import-program`.

The called program runs until it transitions to `end`. When it
terminates, control returns to the caller and `run` returns the callee's
return value.

Running another state machine does not inherit the caller's current
context. Values must be passed explicitly by value or by reference.
Without explicit arguments, the sub-state machine cannot see the upper
level state machine.

Unlike transitioning to a state in another module, `run` does not hand
off program lifetime. When the called program terminates, execution
returns to the caller program.

Examples:

```lisp
(define worker (import-program app worker))
(run worker empty-list)
```

## Data Types

Maestro supports the following runtime data types:

1. integers
2. floats
3. strings
4. lists
5. data objects
6. symbols
7. booleans
8. references
9. states
10. macros

Except for symbols, references, states, and macros, they should all map
cleanly to JSON.

### Integers

Integers are stored explicitly as `int64_t`.

Examples:

```lisp
0
42
-7
```

### Floats

Floats are stored explicitly as `float`.

Examples:

```lisp
3.14
-0.5
```

### Strings

Strings are written in double quotes.

Example:

```lisp
"hello"
```

The empty string literal is `empty-string`.

Example:

```lisp
(= "" empty-string)
```

### Lists

Lists are explicitly constructed, they are not implicitly constructed by
juxtaposition.

Use:

- `list` to construct a list from values
- `cons` to prepend a value to an existing list

Examples:

```lisp
(list 1 2 3)
(cons 0 (list 1 2 3))
```

The empty list literal is `empty-list`.

### Data Objects

Data objects are constructed and updated through `let` and `set`.

The empty data object literal is `empty-object`.

Inline JSON object snippets are also allowed in the language. A JSON
object snippet produces a data object.

JSON snippets may contain Maestro code in value position, including
function calls and value references.

JSON snippets may only evaluate to:

- numbers
- strings
- lists containing valid JSON snippet values
- data objects containing valid JSON snippet values

This is checked at runtime.

JSON snippets may not contain symbols, with one exception: a list of
symbols such as `(list 'sym-a 'sym-b)` may be evaluated as a list of
strings.

Example:

```lisp
(let user empty-object)
(set user profile name "Ada")
(set user profile age 37)
(let parsed-user {"name":"Ada","age":37})
(let age 37)
(let computed-user {"name":"Ada","age":(+ age 1)})
```

### Symbols

Symbols are atomic identifier values.

They are written as:

```lisp
'identifier
```

Symbols are not strings.

Example:

```lisp
'open
'closed
```

A lone symbol does not map cleanly to JSON. A list of symbols exported
to JSON becomes a list of string values.

Example:

```lisp
(list 'open 'closed)
```

becomes:

```json
["open", "closed"]
```

### Booleans

Booleans are the reserved literals:

- `true`
- `false`

### References

References are real runtime values.

They may only be introduced in two places:

- `let` expressions
- macro parameter declarations

Example:

```lisp
(let user empty-object)
(set user profile age 37)
(let r (ref user profile age))

(define (inc-age (ref age))
  (set age (+ age 1)))

(inc-age r)
```

References compare by value with `=` and compare by identity with
`ref=?`.

## Runtime Type Predicates

The built-in type predicates are:

- `number?`
- `integer?`
- `float?`
- `string?`
- `list?`
- `object?`
- `symbol?`
- `boolean?`
- `ref?`
- `state?`
- `macro?`

Examples:

```lisp
(integer? 42)
(float? 3.14)
(symbol? 'idle)
(let age-ref (ref user profile age))
(ref? age-ref)
(state? start)
(macro? concat)
```

Type-check timing:

- parse-time: arity only
- runtime: the predicate inspects the evaluated value and returns a boolean

## Built-in Value Predicates

The built-in value predicates are:

- `empty?`
- `true?`
- `false?`

Example:

```lisp
(empty? empty-string)
(empty? empty-list)
(empty? empty-object)
(true? true)
(false? false)
```

## Truthiness

Maestro uses explicit booleans, but values may be converted to booleans
when required by predicates and boolean operators.

The rules are:

- `false` is false
- `empty-string` is false
- `empty-list` is false
- `empty-object` is false
- strings are true if their length is not `0`
- numbers are true if they are non-zero
- symbols are always true
- non-empty lists are true
- non-empty data objects are true

`empty?` returns true for all `empty-*` values.

An empty string does not equal an empty list, and an empty list does not
equal an empty data object.

## Equality and Comparison

`=` checks for both type and value.

The rules are:

- booleans compare as booleans
- symbols compare by identifier name
- strings compare by exact value
- lists compare by exact value
- data objects compare structurally
- references compare by referenced value
- states compare by referenced state value
- macros compare by referenced macro value
- different types are not equal

Numeric comparison promotion happens automatically.

Arithmetic between integers evaluates to an integer, otherwise it
produces a float. Numeric comparison follows the same promotion rules.

References may also be compared by identity using `ref=?`.

Examples:

```lisp
(= 1 1)
(= 1 1.0)
(= 'open 'open)
(ref=? left right)
```

The built-in comparison operators are:

- `=`
- `!=`
- `<`
- `<=`
- `>`
- `>=`
- `ref=?`

All comparison operators return booleans.

Type-check timing:

- parse-time: arity only
- runtime: numeric promotion, type compatibility, reference value
  comparison, state and macro reference comparison, and reference
  identity comparison

## Boolean Operators

The built-in boolean operators are:

- `and`
- `or`
- `not`

`and` and `or` short-circuit and return booleans.

`not` returns a boolean. Non-boolean arguments are first converted to
booleans using the normal truthiness rules.

Type-check timing:

- parse-time: arity only
- runtime: truthiness conversion and boolean evaluation

Examples:

```lisp
(and true false)
(or false "hello")
(not empty-list)
```

## Arithmetic

The built-in arithmetic operators are:

- `+`
- `-`
- `*`
- `/`
- `%`

Arithmetic is strongly typed for numbers.

Arithmetic operators accept any number of arguments, but at least two.
They are evaluated in the given order.

Strings, lists, data objects, symbols, booleans, and references are not
valid arithmetic operands and should result in a runtime `ERROR`.

Valid division always produces a float internally.

Modulo follows C remainder semantics and is only valid for integers.

Divide by zero results in a runtime `ERROR`.

Type-check timing:

- parse-time: arity must be at least two
- runtime: numeric type-checking, numeric promotion, modulo integer
  validation, and divide-by-zero detection

Examples:

```lisp
(+ 1 2)
(+ 1 2.5)
(/ 4 2)
(% 9 4)
```

### Numeric Conversion

The explicit numeric conversion helpers are:

- `ceil`
- `floor`

`ceil` and `floor` convert floats to integers. Applied to integers, they
return the integer value unchanged.

Type-check timing:

- parse-time: arity only
- runtime: numeric type-checking and float-to-integer conversion

Examples:

```lisp
(floor 3.8)
(ceil 3.2)
(floor 7)
```

## String and List Processing

The built-in primitives are:

- `substr`
- `concat`
- `to-string`

### substr

`substr` accepts three arguments:

- `l`
- `r`
- `str`

It returns the substring `str[l, r)`.

`substr` is zero-indexed and invalid indices result in a runtime
`ERROR`.

Type-check timing:

- parse-time: arity only
- runtime: integer index checking, string type-checking, and bounds
  checking

Example:

```lisp
(substr 0 5 "hello world")
```

### concat

`concat` is defined for both strings and lists.

`concat` accepts any number of arguments, but at least two. Arguments
are concatenated in the given order.

For strings:

- the arguments must be strings
- the result is a string

For lists:

- the right-hand argument is appended to the list
- the result is a list

Type-check timing:

- parse-time: arity must be at least two
- runtime: string-vs-list dispatch and operand type-checking

Examples:

```lisp
(concat "hello, " "world")
(concat (list 1 2) 3)
```

### to-string

`to-string` converts numbers and symbols to strings.

It is not defined for lists, data objects, booleans, or references.

Examples:

```lisp
(to-string 42)
(to-string 3.14)
(to-string 'idle)
```

Type-check timing:

- parse-time: arity only
- runtime: number-or-symbol type-checking and string conversion

## Data Object Access

Data objects are accessed by path. `get`, `let`, `set`, and `ref` all
share the same path trait.

The path forms are:

```lisp
(get data-object-identifier path ...)
(ref data-object-identifier path ...)
(set data-object-identifier path ... value)
(let data-object-identifier path ... value)
```

`get` returns a constant view.

`ref` returns a mutable reference value.

`set` auto-creates missing intermediate object nodes when assigning by
path.

Path-based `let` is equivalent to `set`.

If a path is missing in `get` or `ref`, the runtime must raise an
`ERROR`.

Type-check timing:

- parse-time: form shape only
- runtime: object path resolution, missing-path detection, intermediate
  object creation for `set`, and reference creation

Examples:

```lisp
(let user empty-object)
(set user profile name "Ada")
(get user profile name)
(let age-ref (ref user profile age))
(let user profile age 38)
(let foo val "string")
(set foo val 1)
```

## let and set

`let` introduces or replaces a binding.

If no path is given, the form is:

```lisp
(let var value)
```

This binds or replaces `var` explicitly.

If a path is given, the form is:

```lisp
(let var path ... value)
```

This is equivalent to `set`.

Examples:

```lisp
(let answer 42)
(let answer 43)
```

`set` changes the value of an existing binding.

`set` may rebind the type of the value it assigns.

If the binding is a reference, `set` writes through the reference like a
C++ reference.

Example:

```lisp
(let user empty-object)
(set user profile age 37)
(let r (ref user profile age))
(set r 38)
```

If `set` cannot resolve its target binding, it is an error.

Type-check timing:

- parse-time: binding form shape only
- runtime: binding resolution, reference assignment, and path resolution

## Case Expressions

Conditional branching uses `case`.

Syntax:

```lisp
(case
  ((predicate1) action1)
  ((predicate2) action2)
  (default actionN))
```

Rules:

- predicates are evaluated top-down
- the first true predicate is taken
- `default` must exist
- `default` must be the last clause
- only one `default` is allowed

Example:

```lisp
(case
  ((= state 'idle) (transition wait))
  ((= state 'done) (transition exit))
  (default (transition error)))
```

## External Tools

Tools represent external capabilities and are declared with `external`.

Example:

```lisp
(define (search query) external)
```

The runtime expects the host environment to provide implementations.

## Output

The built-in output primitives are:

- `log`
- `print`

They are bound by the embedding user during VM initialization.

By default:

- `log` binds to printing to `stderr`
- `print` binds to printing to `stdout`

They are bound separately and both use this C function shape:

```c
int (*maestro_output)(maestro_ctx *ctx, char *)
```

Both `log` and `print` return the raw integer from the bound C
function.

`log` and `print` only accept string values.

Type-check timing:

- parse-time: arity only
- runtime: output binding dispatch, argument evaluation, and string
  type-checking

Examples:

```lisp
(print "hello")
(log "debug")
(print (to-string 42))
```

## JSON Helpers

Maestro provides the following JSON helpers:

- `json-parse`
- `json`
- `json-list`

These are meant to help interop with the outside world.

`json-parse` parses a JSON object provided as a string and returns a data
object.

`json` accepts an inline JSON object snippet and returns a data object.
JSON snippets may contain Maestro code in value position.
JSON snippet values are checked at runtime and may only evaluate to
numbers, strings, lists containing valid JSON snippet values, or data
objects containing valid JSON snippet values.
JSON snippets may not contain symbols, except that a list of symbols may
be evaluated as a list of strings.

`json-list` accepts a Maestro list and returns a JSON array value.

Examples:

```lisp
(json-parse "{\"name\":\"Ada\"}")
(json {"name":"Ada","age":37})
(json {"name":"Ada","age":(+ 30 7)})
(json-list (list 1 2 3))
```

Generated JSON examples:

```json
{"name":"Ada","age":37}
```

```json
[1,2,3]
```

## Usage Examples

### Simple Arithmetic

```lisp
(module examples arithmetic)

(state (start)
  (steps
    (print (to-string (+ 1 2)))
    (print (to-string (/ 5 2)))
    (transition start)))
```

### Lists and Strings

```lisp
(module examples data)

(state (start)
  (steps
    (print (substr 0 5 "hello world"))
    (print (concat "mae" "stro"))
    (let values (concat (list 1 2) 3))
    (transition start)))
```

### Data Objects and References

```lisp
(module examples refs)

(define (birthday (ref age))
  (set age (+ age 1)))

(state (start)
  (steps
    (let user empty-object)
    (set user profile name "Ada")
    (set user profile age 37)
    (birthday age)
    (print (to-string (get user profile age)))
    (transition start)))
```

### Case and Booleans

```lisp
(module examples control)

(state (start)
  (steps
    (let state 'idle)
    (case
      ((and true (= state 'idle)) (print "waiting"))
      ((false? false) (print "never"))
      (default (print "fallback")))
    (transition start)))
```
