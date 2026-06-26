    /* Also start shared memory streaming */
    if (shm_sink)
    {
        try {
            if (d_decim >= 2)
                tb->connect(input_decim, 0, gr::basic_block_sptr(shm_sink), 0);
            else
                tb->connect(src, 0, gr::basic_block_sptr(shm_sink), 0);
            std::cout << "Shared memory I/Q streaming started" << std::endl;
        }
        catch (const std::exception &e) {
            std::cerr << "Warning: Could not connect shared memory sink: " << e.what() << std::endl;
        }
    }
