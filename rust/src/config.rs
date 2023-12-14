use crate::bindings;

// The Rust wrapper for ndb_config
pub struct NdbConfig {
    pub config: bindings::ndb_config,
}

impl NdbConfig {
    // Constructor
    pub fn new() -> Self {
        let mut config = bindings::ndb_config {
            filter_context: std::ptr::null_mut(),
            ingest_filter: None,
            flags: 0,
            ingester_threads: 0,
            mapsize: 0,
        };

        unsafe {
            bindings::ndb_default_config(&mut config);
        }

        NdbConfig { config }
    }

    // Example setter methods
    pub fn set_flags(&mut self, flags: i32) -> &mut Self {
        self.config.flags = flags;
        self
    }

    pub fn set_ingester_threads(&mut self, threads: i32) -> &mut Self {
        self.config.ingester_threads = threads;
        self
    }

    // Add other setter methods as needed

    // Internal method to get a raw pointer to the config, used in Ndb
    pub fn as_ptr(&self) -> *const bindings::ndb_config {
        &self.config
    }
}
