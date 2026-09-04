### Missing features
- TLS block alignment
- DT_TEXTREL for partial and full relro
- DF_ORIGIN and any path substitutions
- semicolon handling in LD_LIBRARY_PATH
### Potential future features
- Handling of dynamic TLS where possible, \
currently everything gets statically mapped (behind the TCB into the base image) 
- Lazy binding, currently always resolve everything even without DT_BIND_NOW \
Lazy binding was avoided because of thread-safety concerns

### List of dynamic relocations
- [x] `R_386_NONE`
- [x] `R_386_32`
- [x] `R_386_PC32`
- [x] `R_386_COPY`
- [x] `R_386_GLOB_DAT`
- [x] `R_386_JUMP_SLOT`
- [x] `R_386_RELATIVE`
- [x] `R_386_GOTOFF`
- [x] `R_386_GOTPC`
- [x] `R_386_TLS_TPOFF`
- [x] `R_386_16` - No ABI conforming application uses these 
- [x] `R_386_PC16` - No ABI conforming application uses these 
- [x] `R_386_8` - No ABI conforming application uses these 
- [x] `R_386_PC8` - No ABI conforming application uses these
- [x] `R_386_TLS_DTPMOD32`
- [x] `R_386_TLS_DTPOFF32`
- [x] `R_386_TLS_TPOFF32`
- [x] `R_386_SIZE32`
- [ ] `R_386_TLS_DESC`
- [ ] `R_386_IRELATIVE`
