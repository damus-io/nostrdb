#[allow(non_upper_case_globals)]
#[allow(non_camel_case_types)]
#[allow(non_snake_case)]
#[allow(unused)]
mod bindings;

#[cfg(test)]
mod tests {
    use super::*;
    use bindings as ndb;
    use std::ffi::CString;

    #[test]
    fn ndb_init_works() {
        unsafe {
            // Initialize ndb
            let mut ndb_ptr: *mut bindings::ndb = std::ptr::null_mut();
            let mut config = ndb::ndb_config {
                filter_context: std::ptr::null_mut(),
                ingest_filter: None,
                flags: 0,
                ingester_threads: 0,
                mapsize: 0,
            };

            let path = CString::new(".").expect("Failed to create CString");
            ndb::ndb_default_config(&mut config);
            ndb::ndb_init(&mut ndb_ptr, path.as_ptr(), &mut config);

            // Clean up
            bindings::ndb_destroy(ndb_ptr);
        }
    }
}
