set(component_srcs rf69.c)

idf_component_register(SRCS "rf69.c" "${component_srcs}"
                       PRIV_REQUIRES driver
                       INCLUDE_DIRS ".")
