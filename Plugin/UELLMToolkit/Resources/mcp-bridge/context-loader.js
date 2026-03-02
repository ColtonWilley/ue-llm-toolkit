/**
 * UnrealClaude Dynamic Context Loader
 *
 * Loads UE 5.7 context files based on:
 * 1. Tool names (automatic injection)
 * 2. Query keywords (explicit request)
 *
 * Context files are stored in ./contexts/*.md
 */

import { readFileSync, readdirSync, existsSync } from "fs";
import { join, dirname, basename } from "path";
import { fileURLToPath } from "url";

const __dirname = dirname(fileURLToPath(import.meta.url));
const CONTEXTS_DIR = join(__dirname, "contexts");
const DOMAINS_DIR = join(__dirname, "..", "domains");

// Project-specific domains directory — set via setProjectRoot()
let projectDomainsDir = null;

/**
 * Context configuration
 * Maps categories to files and matching patterns
 */
const CONTEXT_CONFIG = {
  animation: {
    files: ["animation.md"],
    // Tool name patterns that trigger this context
    toolPatterns: [/^anim/, /animation/, /state_machine/],
    // Keywords in user queries that trigger this context
    keywords: [
      "animation",
      "anim",
      "state machine",
      "blend",
      "transition",
      "animinstance",
      "montage",
      "sequence",
      "blendspace",
    ],
  },
  blueprint: {
    files: ["blueprint.md"],
    toolPatterns: [/^blueprint/, /^bp_/],
    keywords: [
      "blueprint",
      "graph",
      "node",
      "pin",
      "uk2node",
      "variable",
      "function",
      "event graph",
    ],
  },
  slate: {
    files: ["slate.md"],
    toolPatterns: [/widget/, /editor.*ui/, /slate/],
    keywords: [
      "slate",
      "widget",
      "snew",
      "sassign",
      "ui",
      "editor window",
      "tab",
      "panel",
      "sverticalbox",
      "shorizontalbox",
    ],
  },
  actor: {
    files: ["actor.md"],
    toolPatterns: [/spawn/, /actor/, /move/, /delete/, /level/, /open_level/],
    keywords: [
      "actor",
      "spawn",
      "component",
      "transform",
      "location",
      "rotation",
      "attach",
      "destroy",
      "iterate",
      "level",
      "map",
      "open level",
      "new level",
      "load map",
      "switch level",
      "template map",
      "save level",
      "save map",
      "save as",
    ],
  },
  assets: {
    files: ["assets.md"],
    toolPatterns: [/asset/, /load/, /reference/],
    keywords: [
      "asset",
      "load",
      "soft pointer",
      "tsoftobjectptr",
      "async",
      "stream",
      "reference",
      "registry",
      "tobjectptr",
    ],
  },
  replication: {
    files: ["replication.md"],
    toolPatterns: [/replicate/, /network/, /rpc/],
    keywords: [
      "replicate",
      "replication",
      "network",
      "rpc",
      "server",
      "client",
      "multicast",
      "onrep",
      "doreplifetime",
      "authority",
    ],
  },
  enhanced_input: {
    files: ["enhanced_input.md"],
    toolPatterns: [/enhanced_input/, /input_action/, /mapping_context/],
    keywords: [
      "enhanced input",
      "input action",
      "mapping context",
      "inputaction",
      "inputmappingcontext",
      "trigger",
      "modifier",
      "key binding",
      "keybinding",
      "gamepad",
      "controller",
      "keyboard mapping",
      "input mapping",
      "dead zone",
      "deadzone",
      "axis",
      "chord",
    ],
  },
  character: {
    files: ["character.md"],
    toolPatterns: [/^character/, /character_data/, /movement_param/],
    keywords: [
      "character",
      "acharacter",
      "movement",
      "charactermovement",
      "walk speed",
      "jump velocity",
      "air control",
      "gravity scale",
      "capsule",
      "character data",
      "stats table",
      "character config",
      "health",
      "stamina",
      "damage multiplier",
      "defense",
      "player character",
      "npc",
    ],
  },
  material: {
    files: ["material.md"],
    toolPatterns: [/^material/, /skeletal_mesh_material/, /actor_material/],
    keywords: [
      "material",
      "material instance",
      "materialinstance",
      "mic",
      "mid",
      "scalar parameter",
      "vector parameter",
      "texture parameter",
      "parent material",
      "material slot",
      "roughness",
      "metallic",
      "base color",
      "emissive",
      "opacity",
    ],
  },
  parallel_workflows: {
    files: ["parallel_workflows.md"],
    toolPatterns: [],
    keywords: [
      "parallel",
      "subagent",
      "swarm",
      "agent team",
      "level setup",
      "build a level",
      "set up a level",
      "create a level",
      "build a scene",
      "set up scene",
      "scene setup",
      "character pipeline",
      "set up character",
      "create character pipeline",
      "multiple agents",
      "decompose",
      "parallelize",
      "concurrent",
      "batch operations",
      "bulk create",
    ],
  },

  // Domain knowledge files (operational workflows, not API reference)
  domain_code: {
    files: ["code.md"],
    toolPatterns: [],
    keywords: [
      "c++ patterns",
      "cmc subclass",
      "character movement component",
      "build strategy",
      "live coding",
      "full rebuild",
      "tobjectptr",
      "struct layout",
      "forward declare",
      "module dependencies",
      "createdefaultsubobject",
    ],
  },
  domain_blueprints: {
    files: ["blueprints.md"],
    toolPatterns: [],
    keywords: [
      "node id",
      "state-bound",
      "state bound graph",
      "animbp creation",
      "create animbp",
      "layout_graph",
      "auto layout",
      "cdo default",
      "blend space binding",
      "parameter_bindings",
      "comparison_chain",
      "transition duration",
      "_c suffix",
      "layer interface",
    ],
  },
  domain_assets: {
    files: ["assets.md"],
    toolPatterns: [],
    keywords: [
      "fbx import",
      "retarget",
      "ik rig",
      "skeleton chain",
      "blender roundtrip",
      "blender fbx",
      "custom_sample_rate",
      "frame rate",
      "capture viewport",
      "visual inspection",
      "root motion",
      "import_rotation",
      "interchange pipeline",
    ],
  },
  domain_debug: {
    files: ["debug.md"],
    toolPatterns: [],
    keywords: [
      "pie automation",
      "run_sequence",
      "input injection",
      "input_tape",
      "hold step",
      "digital action",
      "auto capture",
      "output log cursor",
      "since cursor",
      "post-hoc log",
      "monitor mode",
      "pie status",
      "delay_ms",
    ],
  },
  domain_tooling: {
    files: ["tooling.md"],
    toolPatterns: [],
    keywords: [
      "plugin architecture",
      "extension pattern",
      "new tool",
      "new operation",
      "fmcptoolbase",
      "tool registry",
      "game thread dispatch",
      "t3d export",
      "capability map",
      "property system",
      "cdo vs placed",
      "save after write",
      "parameter validation",
    ],
  },
  domain_meta: {
    files: ["meta.md"],
    toolPatterns: [],
    keywords: [
      "domain system",
      "domain knowledge",
      "module table",
      "loading instructions",
      "hard rules",
      "project-specific knowledge",
    ],
  },
};

// Cache for loaded context files
const contextCache = new Map();

/**
 * Load a context file from disk (with caching)
 * Checks: contexts/ → domains/ → projectDomains/
 */
function loadContextFile(filename) {
  if (contextCache.has(filename)) {
    return contextCache.get(filename);
  }

  // Resolve from contexts/ first, then domains/, then project domains
  let filepath = join(CONTEXTS_DIR, filename);
  if (!existsSync(filepath)) {
    filepath = join(DOMAINS_DIR, filename);
    if (!existsSync(filepath)) {
      if (projectDomainsDir) {
        filepath = join(projectDomainsDir, filename);
        if (!existsSync(filepath)) {
          console.error(`[ContextLoader] Context file not found: ${filename}`);
          return null;
        }
      } else {
        console.error(`[ContextLoader] Context file not found: ${filename}`);
        return null;
      }
    }
  }

  try {
    const content = readFileSync(filepath, "utf-8");
    contextCache.set(filename, content);
    return content;
  } catch (error) {
    console.error(`[ContextLoader] Error loading ${filename}:`, error.message);
    return null;
  }
}

/**
 * Load a project domain file (from project domains directory only)
 * Uses a separate cache key prefix to avoid collisions with plugin files
 */
function loadProjectDomainFile(filename) {
  if (!projectDomainsDir) return null;

  const cacheKey = `project:${filename}`;
  if (contextCache.has(cacheKey)) {
    return contextCache.get(cacheKey);
  }

  const filepath = join(projectDomainsDir, filename);
  if (!existsSync(filepath)) return null;

  try {
    const content = readFileSync(filepath, "utf-8");
    contextCache.set(cacheKey, content);
    return content;
  } catch (error) {
    console.error(`[ContextLoader] Error loading project domain ${filename}:`, error.message);
    return null;
  }
}

/**
 * Get context category from tool name
 * @param {string} toolName - The MCP tool name (without unreal_ prefix)
 * @returns {string|null} - Category name or null
 */
export function getCategoryFromTool(toolName) {
  const lowerName = toolName.toLowerCase();

  for (const [category, config] of Object.entries(CONTEXT_CONFIG)) {
    for (const pattern of config.toolPatterns) {
      if (pattern.test(lowerName)) {
        return category;
      }
    }
  }

  return null;
}

/**
 * Get context categories from a query string
 * @param {string} query - User query or search string
 * @returns {string[]} - Array of matching category names
 */
export function getCategoriesFromQuery(query) {
  const lowerQuery = query.toLowerCase();
  const matches = [];

  for (const [category, config] of Object.entries(CONTEXT_CONFIG)) {
    for (const keyword of config.keywords) {
      if (lowerQuery.includes(keyword.toLowerCase())) {
        if (!matches.includes(category)) {
          matches.push(category);
        }
        break; // One match per category is enough
      }
    }
  }

  return matches;
}

/**
 * Load context content for a specific category
 * For domain_* categories, also loads matching project domain file if available.
 * @param {string} category - Category name (animation, blueprint, etc.)
 * @returns {string|null} - Combined context content or null
 */
export function loadContextForCategory(category) {
  const config = CONTEXT_CONFIG[category];
  if (!config) {
    // Check if it's a dynamically discovered project domain
    if (projectDomainsDir) {
      // Strip domain_ prefix: "domain_combat" -> "combat.md"
      const filename = category.startsWith("domain_")
        ? `${category.substring(7)}.md`
        : `${category}.md`;
      const projectContent = loadProjectDomainFile(filename);
      if (projectContent) return projectContent;
    }
    return null;
  }

  const contents = [];
  for (const file of config.files) {
    const content = loadContextFile(file);
    if (content) {
      contents.push(content);
    }
  }

  // For domain_* categories, also load matching project domain
  if (category.startsWith("domain_") && projectDomainsDir) {
    const domainName = category.substring(7); // "domain_code" -> "code"
    const projectContent = loadProjectDomainFile(`${domainName}.md`);
    if (projectContent) {
      contents.push(projectContent);
    }
  }

  return contents.length > 0 ? contents.join("\n\n---\n\n") : null;
}

/**
 * Load context for a tool call (automatic injection)
 * @param {string} toolName - Tool name (without unreal_ prefix)
 * @returns {string|null} - Context to inject or null
 */
export function getContextForTool(toolName) {
  const category = getCategoryFromTool(toolName);
  if (!category) {
    return null;
  }

  return loadContextForCategory(category);
}

/**
 * Load context based on a query (explicit request)
 * @param {string} query - User query
 * @returns {{ categories: string[], content: string }|null} - Matched contexts
 */
export function getContextForQuery(query) {
  const categories = getCategoriesFromQuery(query);
  if (categories.length === 0) {
    return null;
  }

  const contents = [];
  for (const category of categories) {
    const content = loadContextForCategory(category);
    if (content) {
      contents.push(content);
    }
  }

  return {
    categories,
    content: contents.join("\n\n---\n\n"),
  };
}

/**
 * List all available context categories, including dynamically discovered project domains
 * @returns {string[]} - Array of category names
 */
export function listCategories() {
  const categories = Object.keys(CONTEXT_CONFIG);

  // Discover project-only domain files not already in CONTEXT_CONFIG
  if (projectDomainsDir && existsSync(projectDomainsDir)) {
    try {
      const files = readdirSync(projectDomainsDir);
      for (const file of files) {
        if (!file.endsWith(".md")) continue;
        const name = basename(file, ".md");
        const domainCategory = `domain_${name}`;
        // Skip README, .prep-init config, and already-registered categories
        if (name === "README" || name.startsWith(".")) continue;
        if (!categories.includes(domainCategory)) {
          categories.push(domainCategory);
        }
      }
    } catch (error) {
      console.error(`[ContextLoader] Error scanning project domains:`, error.message);
    }
  }

  return categories;
}

/**
 * Get metadata about a category
 * @param {string} category - Category name
 * @returns {object|null} - Category metadata
 */
export function getCategoryInfo(category) {
  const config = CONTEXT_CONFIG[category];
  if (!config) {
    return null;
  }

  return {
    name: category,
    files: config.files,
    keywords: config.keywords,
    toolPatterns: config.toolPatterns.map((p) => p.toString()),
  };
}

/**
 * Set the project root directory for project-specific domain loading.
 * Expects the directory containing the .uproject file.
 * Project domains are looked up in <projectRoot>/domains/
 * @param {string} projectRoot - Absolute path to project root
 */
export function setProjectRoot(projectRoot) {
  if (!projectRoot) return;

  const domainsPath = join(projectRoot, "domains");
  if (existsSync(domainsPath)) {
    projectDomainsDir = domainsPath;
    contextCache.clear(); // Clear cache so project domains are picked up
    console.error(`[ContextLoader] Project domains loaded from: ${domainsPath}`);
  } else {
    console.error(`[ContextLoader] No project domains directory at: ${domainsPath}`);
  }
}

/**
 * Get the current project domains directory (for diagnostics)
 * @returns {string|null}
 */
export function getProjectDomainsDir() {
  return projectDomainsDir;
}

/**
 * Clear the context cache (useful for hot-reloading)
 */
export function clearCache() {
  contextCache.clear();
}

export default {
  getCategoryFromTool,
  getCategoriesFromQuery,
  loadContextForCategory,
  getContextForTool,
  getContextForQuery,
  listCategories,
  getCategoryInfo,
  setProjectRoot,
  getProjectDomainsDir,
  clearCache,
};
