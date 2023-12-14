use std::ffi::CString;
use std::ptr;

use crate::bindings;
use crate::config::NdbConfig;
use crate::error::Error;
use crate::result::Result;

pub struct Ndb {
    ndb: *mut bindings::ndb,
}

impl Ndb {
    // Constructor
    pub fn new(db_dir: &str, config: &NdbConfig) -> Result<Self> {
        let db_dir_cstr = match CString::new(db_dir) {
            Ok(cstr) => cstr,
            Err(_) => return Err(Error::DbOpenFailed),
        };
        let mut ndb: *mut bindings::ndb = ptr::null_mut();
        let result = unsafe { bindings::ndb_init(&mut ndb, db_dir_cstr.as_ptr(), config.as_ptr()) };

        if result != 0 {
            return Err(Error::DbOpenFailed);
        }

        Ok(Ndb { ndb })
    }

    // Add other methods to interact with the library here
}

impl Drop for Ndb {
    fn drop(&mut self) {
        unsafe {
            bindings::ndb_destroy(self.ndb);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;

    fn cleanup() {
        let _ = fs::remove_file("data.mdb");
        let _ = fs::remove_file("lock.mdb");
    }

    #[test]
    fn ndb_init_works() {
        // Initialize ndb
        {
            let cfg = NdbConfig::new();
            let _ = Ndb::new(".", &cfg);
        }

        cleanup();
    }
}
