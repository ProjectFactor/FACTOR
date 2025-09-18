package=gmp
$(package)_version=6.3.0
$(package)_download_path=https://gmplib.org/download/$(package)/
$(package)_file_name=$(package)-$($(package)_version).tar.xz
$(package)_sha256_hash=a3c2b80201b89e68616f4ad30bc66aee4927c3ce50e33929ca819d5c43538898
$(package)_patches=0001-Complete-function-prototype-in-acinclude.m4-for-C23-.patch

# arm64-avoid-x18.patch is already included in GMP 6.3.0 (https://github.com/gmp-mirror/gmp/commit/1aaf0e4a34b25e73f5505151ab12397031318985)
define $(package)_preprocess_cmds
  patch -p1 < $($(package)_patch_dir)/0001-Complete-function-prototype-in-acinclude.m4-for-C23-.patch && \
  autoconf
endef

define $(package)_set_vars
  $(package)_config_opts=--enable-cxx
endef

define $(package)_config_cmds
  $($(package)_autoconf)
endef

define $(package)_build_cmds
  $(MAKE)
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install
endef
