# Immutable input data for the installed E-UNION regression fixtures.
# Install explicitly with --component tests-data; ordinary installation omits it.
find_package(Python3 REQUIRED COMPONENTS Interpreter)

install(FILES "data/eunion/direnum/lower-only.txt"
    DESTINATION "libexec/darling/usr/share/darling/tests/eunion/direnum"
    PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ COMPONENT tests-data EXCLUDE_FROM_ALL)

install(FILES "data/eunion/direnum/shadow.txt"
    DESTINATION "libexec/darling/usr/share/darling/tests/eunion/direnum"
    PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ COMPONENT tests-data EXCLUDE_FROM_ALL)

install(FILES "data/eunion/metadata/fdmeta.txt"
    DESTINATION "libexec/darling/usr/share/darling/tests/eunion/metadata"
    PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ COMPONENT tests-data EXCLUDE_FROM_ALL)

install(FILES "data/eunion/metadata/fdxattr.txt"
    DESTINATION "libexec/darling/usr/share/darling/tests/eunion/metadata"
    PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ COMPONENT tests-data EXCLUDE_FROM_ALL)

install(FILES "data/eunion/metadata/ftrunc.txt"
    DESTINATION "libexec/darling/usr/share/darling/tests/eunion/metadata"
    PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ COMPONENT tests-data EXCLUDE_FROM_ALL)

install(FILES "data/eunion/metadata/pathxattr.txt"
    DESTINATION "libexec/darling/usr/share/darling/tests/eunion/metadata"
    PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ COMPONENT tests-data EXCLUDE_FROM_ALL)

install(FILES "data/eunion/metadata/setid.txt"
    DESTINATION "libexec/darling/usr/share/darling/tests/eunion/metadata"
    PERMISSIONS SETUID OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE COMPONENT tests-data EXCLUDE_FROM_ALL)

install(CODE "
    execute_process(
        COMMAND \"${Python3_EXECUTABLE}\" -c \"import os,sys; os.setxattr(sys.argv[1], sys.argv[2], sys.argv[3].encode())\"
            \"\$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/darling/usr/share/darling/tests/eunion/metadata/setid.txt\" \"user.test.tag\" \"hello\"
        RESULT_VARIABLE xattr_result)
    if(NOT xattr_result EQUAL 0)
        message(FATAL_ERROR \"Cannot install E-UNION fixture xattr user.test.tag\")
    endif()
" COMPONENT tests-data EXCLUDE_FROM_ALL)

install(FILES "data/eunion/mkdir-opaque/recreate/stale_a.txt"
    DESTINATION "libexec/darling/usr/share/darling/tests/eunion/mkdir-opaque/recreate"
    PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ COMPONENT tests-data EXCLUDE_FROM_ALL)

install(FILES "data/eunion/mkdir-opaque/recreate/stale_b.txt"
    DESTINATION "libexec/darling/usr/share/darling/tests/eunion/mkdir-opaque/recreate"
    PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ COMPONENT tests-data EXCLUDE_FROM_ALL)

install(FILES "data/eunion/mutation/bind_parent/.keep"
    DESTINATION "libexec/darling/usr/share/darling/tests/eunion/mutation/bind_parent"
    PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ COMPONENT tests-data EXCLUDE_FROM_ALL)

install(FILES "data/eunion/mutation/copy.txt"
    DESTINATION "libexec/darling/usr/share/darling/tests/eunion/mutation"
    PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ COMPONENT tests-data EXCLUDE_FROM_ALL)

install(FILES "data/eunion/mutation/create_parent/.keep"
    DESTINATION "libexec/darling/usr/share/darling/tests/eunion/mutation/create_parent"
    PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ COMPONENT tests-data EXCLUDE_FROM_ALL)

install(FILES "data/eunion/mutation/delete.txt"
    DESTINATION "libexec/darling/usr/share/darling/tests/eunion/mutation"
    PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ COMPONENT tests-data EXCLUDE_FROM_ALL)

install(FILES "data/eunion/mutation/rename_src/child.txt"
    DESTINATION "libexec/darling/usr/share/darling/tests/eunion/mutation/rename_src"
    PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ COMPONENT tests-data EXCLUDE_FROM_ALL)

install(FILES "data/eunion/mutation/sp_real/.keep"
    DESTINATION "libexec/darling/usr/share/darling/tests/eunion/mutation/sp_real"
    PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ COMPONENT tests-data EXCLUDE_FROM_ALL)

install(FILES "data/eunion/nested-bind/d/t"
    DESTINATION "libexec/darling/usr/share/darling/tests/eunion/nested-bind/d"
    PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ COMPONENT tests-data EXCLUDE_FROM_ALL)

install(FILES "data/eunion/nested-bind/r/t"
    DESTINATION "libexec/darling/usr/share/darling/tests/eunion/nested-bind/r"
    PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ COMPONENT tests-data EXCLUDE_FROM_ALL)

install(FILES "data/eunion/path-errors/shadow_dir/lower.txt"
    DESTINATION "libexec/darling/usr/share/darling/tests/eunion/path-errors/shadow_dir"
    PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ COMPONENT tests-data EXCLUDE_FROM_ALL)

install(FILES "data/eunion/relative-at/open_existing.txt"
    DESTINATION "libexec/darling/usr/share/darling/tests/eunion/relative-at"
    PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ COMPONENT tests-data EXCLUDE_FROM_ALL)

install(FILES "data/eunion/relative-at/rename_src/child.txt"
    DESTINATION "libexec/darling/usr/share/darling/tests/eunion/relative-at/rename_src"
    PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ COMPONENT tests-data EXCLUDE_FROM_ALL)

install(FILES "data/eunion/relative-at/unlink.txt"
    DESTINATION "libexec/darling/usr/share/darling/tests/eunion/relative-at"
    PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ COMPONENT tests-data EXCLUDE_FROM_ALL)

install(FILES "data/eunion/smoke/lower.txt"
    DESTINATION "libexec/darling/usr/share/darling/tests/eunion/smoke"
    PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ COMPONENT tests-data EXCLUDE_FROM_ALL)

install(FILES "data/eunion/smoke/shadow.txt"
    DESTINATION "libexec/darling/usr/share/darling/tests/eunion/smoke"
    PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ COMPONENT tests-data EXCLUDE_FROM_ALL)

install(FILES "data/eunion/special-create/real/.keep"
    DESTINATION "libexec/darling/usr/share/darling/tests/eunion/special-create/real"
    PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ COMPONENT tests-data EXCLUDE_FROM_ALL)

install(FILES "data/eunion/xattr-policy/fd_remove.txt"
    DESTINATION "libexec/darling/usr/share/darling/tests/eunion/xattr-policy"
    PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ COMPONENT tests-data EXCLUDE_FROM_ALL)

install(CODE "
    execute_process(
        COMMAND \"${Python3_EXECUTABLE}\" -c \"import os,sys; os.setxattr(sys.argv[1], sys.argv[2], sys.argv[3].encode())\"
            \"\$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/darling/usr/share/darling/tests/eunion/xattr-policy/fd_remove.txt\" \"user.test.fdremove\" \"FD_REMOVE_ME\"
        RESULT_VARIABLE xattr_result)
    if(NOT xattr_result EQUAL 0)
        message(FATAL_ERROR \"Cannot install E-UNION fixture xattr user.test.fdremove\")
    endif()
" COMPONENT tests-data EXCLUDE_FROM_ALL)

install(FILES "data/eunion/xattr-policy/opaque/.lower"
    DESTINATION "libexec/darling/usr/share/darling/tests/eunion/xattr-policy/opaque"
    PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ COMPONENT tests-data EXCLUDE_FROM_ALL)

install(FILES "data/eunion/xattr-policy/path_remove.txt"
    DESTINATION "libexec/darling/usr/share/darling/tests/eunion/xattr-policy"
    PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ COMPONENT tests-data EXCLUDE_FROM_ALL)

install(CODE "
    execute_process(
        COMMAND \"${Python3_EXECUTABLE}\" -c \"import os,sys; os.setxattr(sys.argv[1], sys.argv[2], sys.argv[3].encode())\"
            \"\$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/darling/usr/share/darling/tests/eunion/xattr-policy/path_remove.txt\" \"user.test.remove\" \"REMOVE_ME\"
        RESULT_VARIABLE xattr_result)
    if(NOT xattr_result EQUAL 0)
        message(FATAL_ERROR \"Cannot install E-UNION fixture xattr user.test.remove\")
    endif()
" COMPONENT tests-data EXCLUDE_FROM_ALL)

InstallSymlink("sp_real"
    "${CMAKE_INSTALL_PREFIX}/libexec/darling/usr/share/darling/tests/eunion/mutation/sp_link" COMPONENT tests-data EXCLUDE_FROM_ALL)

InstallSymlink("r"
    "${CMAKE_INSTALL_PREFIX}/libexec/darling/usr/share/darling/tests/eunion/nested-bind/l" COMPONENT tests-data EXCLUDE_FROM_ALL)

InstallSymlink("../../../../../../../escape-target"
    "${CMAKE_INSTALL_PREFIX}/libexec/darling/usr/share/darling/tests/eunion/path-errors/escape_link" COMPONENT tests-data EXCLUDE_FROM_ALL)

InstallSymlink("real"
    "${CMAKE_INSTALL_PREFIX}/libexec/darling/usr/share/darling/tests/eunion/special-create/link_parent" COMPONENT tests-data EXCLUDE_FROM_ALL)

install(FILES data/eunion/probe/lower.txt data/eunion/probe/shadow.txt
    DESTINATION libexec/darling/usr/share/darling/tests/eunion/probe
    PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ COMPONENT tests-data EXCLUDE_FROM_ALL)
