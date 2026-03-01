-- set minimum xmake version
set_xmakever("2.8.2")

-- enable commonlibsse-ng options
set_config("rex_ini", true)

-- includes
includes("lib/commonlibsse-ng")

-- set project
set_project("Intellightent")
set_license("GPL-3.0")

local version = "2.0.0"
local ver = version:split("%.")
set_version(version)

-- set defaults
set_languages("c++23")
set_warnings("allextra")

-- set policies
set_policy("package.requires_lock", true)

-- add rules
add_rules("mode.debug", "mode.releasedbg")
add_rules("plugin.vsxmake.autoupdate")

-- add packages
add_requires("exprtk")

-- targets
target("Intellightent")
-- add dependencies to target
add_deps("commonlibsse-ng")
add_packages("exprtk")

-- set DLL output name
set_basename("intellightent-ng")

-- version config vars
set_configvar("VERSION_MAJOR", tonumber(ver[1]))
set_configvar("VERSION_MINOR", tonumber(ver[2]))
set_configvar("VERSION_PATCH", tonumber(ver[3]))
set_configvar("VERSION_STRING", version)

-- add commonlibsse-ng plugin
add_rules("commonlibsse-ng.plugin", {
    name = "Intellightent",
    author = "libxse",
    description = "Intellightent Skyrim Mod",
})

-- add src files
add_files("src/**.cpp")
add_headerfiles("src/**.h")
add_includedirs("src")
set_pcxxheader("src/pch.h")
add_configfiles("src/Version.h.in")

-- auto deploy: set SkyrimPluginTargets to one or more paths separated by ';'
after_build(function(target)
    local deploy_dirs = os.getenv("SkyrimPluginTargets")
    if not deploy_dirs then
        return
    end
    local dll = target:targetfile()
    local pdb = path.join(path.directory(dll), path.basename(dll) .. ".pdb")
    for _, dir in ipairs(deploy_dirs:split(";")) do
        dir = dir:trim()
        if dir ~= "" then
            local dest = path.join(dir, "SKSE", "Plugins")
            os.mkdir(dest)
            os.cp(dll, dest)
            if os.isfile(pdb) then
                os.cp(pdb, dest)
            end
            print("Deployed to " .. dest)
        end
    end
end)
