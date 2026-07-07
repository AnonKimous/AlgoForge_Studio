# accessRules.md

default: manual

## allow
- contains: explain
- contains: summarize
- contains: generate
- contains: export

## deny
- contains: delete all
- contains: wipe
- contains: format c:
- regex: (?i)\brm\s+-rf\b

## manual
- contains: hot build
- contains: publish
